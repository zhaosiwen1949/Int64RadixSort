/******************************************************************************
 * GPUSorting
 * Device Level 8-bit LSD Radix Sort using reduce then scan
 *
 * SPDX-License-Identifier: MIT
 * Copyright Thomas Smith 3/13/2024
 * https://github.com/b0nes164/GPUSorting
 * 
 ******************************************************************************/
//Compiler Defines
//#define KEY_UINT KEY_INT KEY_FLOAT
//#define PAYLOAD_UINT PAYLOAD_INT PAYLOAD_FLOAT
//#define SHOULD_ASCEND
//#define SORT_PAIRS
//#define ENABLE_16_BIT
//#define LOCK_TO_W32               //Used to lock RDNA to 32, we want WGP's not CU's
#include "SortCommon.hlsl"

#define US_DIM          128U        //The number of threads in a Upsweep threadblock
#define SCAN_DIM        128U        //The number of threads in a Scan threadblock

RWStructuredBuffer<uint> b_globalHist   : register(u4); //buffer holding device level offsets for each binning pass
RWStructuredBuffer<uint> b_passHist     : register(u5); //buffer used to store reduced sums of partition tiles

groupshared uint g_us[RADIX * 2];   //Shared memory for upsweep
groupshared uint g_scan[SCAN_DIM];  //Shared memory for the scan

//*****************************************************************************
//INIT KERNEL
//*****************************************************************************
//Clear the global histogram, as we will be adding to it atomically
[numthreads(1024, 1, 1)]
void InitDeviceRadixSort(int3 id : SV_DispatchThreadID)
{
    b_globalHist[id.x] = 0;
}

//*****************************************************************************
//UPSWEEP KERNEL
//*****************************************************************************
//histogram, 64 threads to a histogram
inline void HistogramDigitCounts(uint gtid, uint gid)
{
    const uint histOffset = gtid / 64 * RADIX;
    const uint partitionEnd = gid == e_threadBlocks - 1 ?
        e_numKeys : (gid + 1) * PART_SIZE;
    for (uint i = gtid + gid * PART_SIZE; i < partitionEnd; i += US_DIM)
    {
#if defined(KEY_UINT)
        InterlockedAdd(g_us[ExtractDigit(b_sort[i]) + histOffset], 1);
#elif defined(KEY_INT)
        InterlockedAdd(g_us[ExtractDigit(IntToUint(b_sort[i])) + histOffset], 1);
#elif defined(KEY_FLOAT)
        InterlockedAdd(g_us[ExtractDigit(FloatToUint(b_sort[i])) + histOffset], 1);
#endif
    }
}

//reduce and pass to tile histogram
inline void ReduceWriteDigitCounts(uint gtid, uint gid)
{
    for (uint i = gtid; i < RADIX; i += US_DIM)
    {
        g_us[i] += g_us[i + RADIX];
        b_passHist[i * e_threadBlocks + gid] = g_us[i];
        g_us[i] += WavePrefixSum(g_us[i]);
    }
}

//Exclusive scan over digit counts, then atomically add to global hist
inline void GlobalHistExclusiveScanWGE16(uint gtid)
{
    GroupMemoryBarrierWithGroupSync();
        
    if (gtid < (RADIX / WaveGetLaneCount()))
    {
        g_us[(gtid + 1) * WaveGetLaneCount() - 1] +=
            WavePrefixSum(g_us[(gtid + 1) * WaveGetLaneCount() - 1]);
    }
    GroupMemoryBarrierWithGroupSync();
        
    //atomically add to global histogram
    const uint globalHistOffset = GlobalHistOffset();
    const uint laneMask = WaveGetLaneCount() - 1;
    const uint circularLaneShift = WaveGetLaneIndex() + 1 & laneMask;
    for (uint i = gtid; i < RADIX; i += US_DIM)
    {
        const uint index = circularLaneShift + (i & ~laneMask);
        InterlockedAdd(b_globalHist[index + globalHistOffset],
            (WaveGetLaneIndex() != laneMask ? g_us[i] : 0) +
            (i >= WaveGetLaneCount() ? WaveReadLaneAt(g_us[i - 1], 0) : 0));
    }
}

inline void GlobalHistExclusiveScanWLT16(uint gtid)
{
    const uint globalHistOffset = GlobalHistOffset();
    if (gtid < WaveGetLaneCount())
    {
        const uint circularLaneShift = WaveGetLaneIndex() + 1 &
            WaveGetLaneCount() - 1;
        InterlockedAdd(b_globalHist[circularLaneShift + globalHistOffset],
            circularLaneShift ? g_us[gtid] : 0);
    }
    GroupMemoryBarrierWithGroupSync();
        
    const uint laneLog = countbits(WaveGetLaneCount() - 1);
    uint offset = laneLog;
    uint j = WaveGetLaneCount();
    for (; j < (RADIX >> 1); j <<= laneLog)
    {
        if (gtid < (RADIX >> offset))
        {
            g_us[((gtid + 1) << offset) - 1] +=
                WavePrefixSum(g_us[((gtid + 1) << offset) - 1]);
        }
        GroupMemoryBarrierWithGroupSync();
            
        for (uint i = gtid + j; i < RADIX; i += US_DIM)
        {
            if ((i & ((j << laneLog) - 1)) >= j)
            {
                if (i < (j << laneLog))
                {
                    InterlockedAdd(b_globalHist[i + globalHistOffset],
                        WaveReadLaneAt(g_us[((i >> offset) << offset) - 1], 0) +
                        ((i & (j - 1)) ? g_us[i - 1] : 0));
                }
                else
                {
                    if ((i + 1) & (j - 1))
                    {
                        g_us[i] +=
                            WaveReadLaneAt(g_us[((i >> offset) << offset) - 1], 0);
                    }
                }
            }
        }
        offset += laneLog;
    }
    GroupMemoryBarrierWithGroupSync();
        
    //If RADIX is not a power of lanecount
    for (uint i = gtid + j; i < RADIX; i += US_DIM)
    {
        InterlockedAdd(b_globalHist[i + globalHistOffset],
            WaveReadLaneAt(g_us[((i >> offset) << offset) - 1], 0) +
            ((i & (j - 1)) ? g_us[i - 1] : 0));
    }
}

[numthreads(US_DIM, 1, 1)]
void Upsweep(uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID)
{
    //clear shared memory
    const uint histsEnd = RADIX * 2;
    for (uint i = gtid.x; i < histsEnd; i += US_DIM)
        g_us[i] = 0;
    GroupMemoryBarrierWithGroupSync();

    HistogramDigitCounts(gtid.x, flattenGid(gid));
    GroupMemoryBarrierWithGroupSync();
    
    ReduceWriteDigitCounts(gtid.x, flattenGid(gid));
    
    if (WaveGetLaneCount() >= 16)
        GlobalHistExclusiveScanWGE16(gtid.x);
    
    if (WaveGetLaneCount() < 16)
        GlobalHistExclusiveScanWLT16(gtid.x);
}

//*****************************************************************************
//SCAN KERNEL
//*****************************************************************************
inline void ExclusiveThreadBlockScanFullWGE16(
    uint gtid,
    uint laneMask,
    uint circularLaneShift,
    uint partEnd,
    uint deviceOffset,
    inout uint reduction)
{
    for (uint i = gtid; i < partEnd; i += SCAN_DIM)
    {
        g_scan[gtid] = b_passHist[i + deviceOffset];
        g_scan[gtid] += WavePrefixSum(g_scan[gtid]);
        GroupMemoryBarrierWithGroupSync();
            
        if (gtid < SCAN_DIM / WaveGetLaneCount())
        {
            g_scan[(gtid + 1) * WaveGetLaneCount() - 1] +=
                WavePrefixSum(g_scan[(gtid + 1) * WaveGetLaneCount() - 1]);
        }
        GroupMemoryBarrierWithGroupSync();
            
        b_passHist[circularLaneShift + (i & ~laneMask) + deviceOffset] =
            (WaveGetLaneIndex() != laneMask ? g_scan[gtid.x] : 0) +
            (gtid.x >= WaveGetLaneCount() ?
            WaveReadLaneAt(g_scan[gtid.x - 1], 0) : 0) +
            reduction;

        reduction += g_scan[SCAN_DIM - 1];
        GroupMemoryBarrierWithGroupSync();
    }
}

inline void ExclusiveThreadBlockScanPartialWGE16(
    uint gtid,
    uint laneMask,
    uint circularLaneShift,
    uint partEnd,
    uint deviceOffset,
    uint reduction)
{
    uint i = gtid + partEnd;
    if (i < e_threadBlocks)
        g_scan[gtid] = b_passHist[deviceOffset + i];
    g_scan[gtid] += WavePrefixSum(g_scan[gtid]);
    GroupMemoryBarrierWithGroupSync();
            
    if (gtid < SCAN_DIM / WaveGetLaneCount())
    {
        g_scan[(gtid + 1) * WaveGetLaneCount() - 1] +=
            WavePrefixSum(g_scan[(gtid + 1) * WaveGetLaneCount() - 1]);
    }
    GroupMemoryBarrierWithGroupSync();
        
    const uint index = circularLaneShift + (i & ~laneMask);
    if (index < e_threadBlocks)
    {
        b_passHist[index + deviceOffset] =
            (WaveGetLaneIndex() != laneMask ? g_scan[gtid.x] : 0) +
            (gtid.x >= WaveGetLaneCount() ?
            g_scan[(gtid.x & ~laneMask) - 1] : 0) +
            reduction;
    }
}

inline void ExclusiveThreadBlockScanWGE16(uint gtid, uint gid)
{
    uint reduction = 0;
    const uint laneMask = WaveGetLaneCount() - 1;
    const uint circularLaneShift = WaveGetLaneIndex() + 1 & laneMask;
    const uint partionsEnd = e_threadBlocks / SCAN_DIM * SCAN_DIM;
    const uint deviceOffset = gid * e_threadBlocks;
    
    ExclusiveThreadBlockScanFullWGE16(
        gtid,
        laneMask,
        circularLaneShift,
        partionsEnd,
        deviceOffset,
        reduction);

    ExclusiveThreadBlockScanPartialWGE16(
        gtid,
        laneMask,
        circularLaneShift,
        partionsEnd,
        deviceOffset,
        reduction);
}

inline void ExclusiveThreadBlockScanFullWLT16(
    uint gtid,
    uint partitions,
    uint deviceOffset,
    uint laneLog,
    uint circularLaneShift,
    inout uint reduction)
{
    for (uint k = 0; k < partitions; ++k)
    {
        g_scan[gtid] = b_passHist[gtid + k * SCAN_DIM + deviceOffset];
        g_scan[gtid] += WavePrefixSum(g_scan[gtid]);
        GroupMemoryBarrierWithGroupSync();
        if (gtid < WaveGetLaneCount())
        {
            b_passHist[circularLaneShift + k * SCAN_DIM + deviceOffset] =
                (circularLaneShift ? g_scan[gtid] : 0) + reduction;
        }
            
        uint offset = laneLog;
        uint j = WaveGetLaneCount();
        for (; j < (SCAN_DIM >> 1); j <<= laneLog)
        {
            if (gtid < (SCAN_DIM >> offset))
            {
                g_scan[((gtid + 1) << offset) - 1] +=
                    WavePrefixSum(g_scan[((gtid + 1) << offset) - 1]);
            }
            GroupMemoryBarrierWithGroupSync();
            
            if ((gtid & ((j << laneLog) - 1)) >= j)
            {
                if (gtid < (j << laneLog))
                {
                    b_passHist[gtid + k * SCAN_DIM + deviceOffset] =
                        WaveReadLaneAt(g_scan[((gtid >> offset) << offset) - 1], 0) +
                        ((gtid & (j - 1)) ? g_scan[gtid - 1] : 0) + reduction;
                }
                else
                {
                    if ((gtid + 1) & (j - 1))
                    {
                        g_scan[gtid] +=
                            WaveReadLaneAt(g_scan[((gtid >> offset) << offset) - 1], 0);
                    }
                }
            }
            offset += laneLog;
        }
        GroupMemoryBarrierWithGroupSync();
        
        //If SCAN_DIM is not a power of lanecount
        for (uint i = gtid + j; i < SCAN_DIM; i += SCAN_DIM)
        {
            b_passHist[i + k * SCAN_DIM + deviceOffset] =
                WaveReadLaneAt(g_scan[((i >> offset) << offset) - 1], 0) +
                ((i & (j - 1)) ? g_scan[i - 1] : 0) + reduction;
        }
            
        reduction += WaveReadLaneAt(g_scan[SCAN_DIM - 1], 0) +
            WaveReadLaneAt(g_scan[(((SCAN_DIM - 1) >> offset) << offset) - 1], 0);
        GroupMemoryBarrierWithGroupSync();
    }
}

inline void ExclusiveThreadBlockScanParitalWLT16(
    uint gtid,
    uint partitions,
    uint deviceOffset,
    uint laneLog,
    uint circularLaneShift,
    uint reduction)
{
    const uint finalPartSize = e_threadBlocks - partitions * SCAN_DIM;
    if (gtid < finalPartSize)
    {
        g_scan[gtid] = b_passHist[gtid + partitions * SCAN_DIM + deviceOffset];
        g_scan[gtid] += WavePrefixSum(g_scan[gtid]);
    }
    GroupMemoryBarrierWithGroupSync();
    if (gtid < WaveGetLaneCount() && circularLaneShift < finalPartSize)
    {
        b_passHist[circularLaneShift + partitions * SCAN_DIM + deviceOffset] =
            (circularLaneShift ? g_scan[gtid] : 0) + reduction;
    }
        
    uint offset = laneLog;
    for (uint j = WaveGetLaneCount(); j < finalPartSize; j <<= laneLog)
    {
        if (gtid < (finalPartSize >> offset))
        {
            g_scan[((gtid + 1) << offset) - 1] +=
                WavePrefixSum(g_scan[((gtid + 1) << offset) - 1]);
        }
        GroupMemoryBarrierWithGroupSync();
            
        if ((gtid & ((j << laneLog) - 1)) >= j && gtid < finalPartSize)
        {
            if (gtid < (j << laneLog))
            {
                b_passHist[gtid + partitions * SCAN_DIM + deviceOffset] =
                    WaveReadLaneAt(g_scan[((gtid >> offset) << offset) - 1], 0) +
                    ((gtid & (j - 1)) ? g_scan[gtid - 1] : 0) + reduction;
            }
            else
            {
                if ((gtid + 1) & (j - 1))
                {
                    g_scan[gtid] +=
                        WaveReadLaneAt(g_scan[((gtid >> offset) << offset) - 1], 0);
                }
            }
        }
        offset += laneLog;
    }
}

inline void ExclusiveThreadBlockScanWLT16(uint gtid, uint gid)
{
    uint reduction = 0;
    const uint partitions = e_threadBlocks / SCAN_DIM;
    const uint deviceOffset = gid * e_threadBlocks;
    const uint laneLog = countbits(WaveGetLaneCount() - 1);
    const uint circularLaneShift = WaveGetLaneIndex() + 1 &
                    WaveGetLaneCount() - 1;
    
    ExclusiveThreadBlockScanFullWLT16(
        gtid,
        partitions,
        deviceOffset,
        laneLog,
        circularLaneShift,
        reduction);
    
    ExclusiveThreadBlockScanParitalWLT16(
        gtid,
        partitions,
        deviceOffset,
        laneLog,
        circularLaneShift,
        reduction);
}

//Scan does not need flattening of gids
[numthreads(SCAN_DIM, 1, 1)]
void Scan(uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID)
{
    if (WaveGetLaneCount() >= 16)
        ExclusiveThreadBlockScanWGE16(gtid.x, gid.x);

    if (WaveGetLaneCount() < 16)
        ExclusiveThreadBlockScanWLT16(gtid.x, gid.x);
}

//*****************************************************************************
//DOWNSWEEP KERNEL
//*****************************************************************************
inline void LoadThreadBlockReductions(uint gtid, uint gid, uint exclusiveHistReduction)
{
    if (gtid < RADIX)
    {
        g_d[gtid + PART_SIZE] = b_globalHist[gtid + GlobalHistOffset()] +
            b_passHist[gtid * e_threadBlocks + gid] - exclusiveHistReduction;
    }
}

//Lock RDNA to 32, we want WGP's not CU's
#if defined(LOCK_TO_W32)
[WaveSize(32)]
#endif
[numthreads(D_DIM, 1, 1)]
void Downsweep(uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID)
{
    KeyStruct keys;
    OffsetStruct offsets;
    
    ClearWaveHists(gtid.x);
    
    if (flattenGid(gid) < e_threadBlocks - 1)
    {
        if (WaveGetLaneCount() >= 16)
            keys = LoadKeysWGE16(gtid.x, flattenGid(gid));
        
        if (WaveGetLaneCount() < 16)
            keys = LoadKeysWLT16(gtid.x, flattenGid(gid), SerialIterations());
    }
        
    if (flattenGid(gid) == e_threadBlocks - 1)
    {
        if (WaveGetLaneCount() >= 16)
            keys = LoadKeysPartialWGE16(gtid.x, flattenGid(gid));
        
        if (WaveGetLaneCount() < 16)
            keys = LoadKeysPartialWLT16(gtid.x, flattenGid(gid), SerialIterations());
    }
    
    uint exclusiveHistReduction;
    if (WaveGetLaneCount() >= 16)
    {
        GroupMemoryBarrierWithGroupSync();

        offsets = RankKeysWGE16(gtid.x, keys);
        GroupMemoryBarrierWithGroupSync();
        
        uint histReduction;
        if (gtid.x < RADIX)
        {
            histReduction = WaveHistInclusiveScanCircularShiftWGE16(gtid.x);
            histReduction += WavePrefixSum(histReduction); //take advantage of barrier to begin scan
        }
        GroupMemoryBarrierWithGroupSync();

        WaveHistReductionExclusiveScanWGE16(gtid.x, histReduction);
        GroupMemoryBarrierWithGroupSync();
            
        UpdateOffsetsWGE16(gtid.x, offsets, keys);
        if (gtid.x < RADIX)
            exclusiveHistReduction = g_d[gtid.x]; //take advantage of barrier to grab value
        GroupMemoryBarrierWithGroupSync();
    }
    
    if (WaveGetLaneCount() < 16)
    {
        offsets = RankKeysWLT16(gtid.x, keys, SerialIterations());
            
        if (gtid.x < HALF_RADIX)
        {
            uint histReduction = WaveHistInclusiveScanCircularShiftWLT16(gtid.x);
            g_d[gtid.x] = histReduction + (histReduction << 16); //take advantage of barrier to begin scan
        }
            
        WaveHistReductionExclusiveScanWLT16(gtid.x);
        GroupMemoryBarrierWithGroupSync();
            
        UpdateOffsetsWLT16(gtid.x, SerialIterations(), offsets, keys);
        if (gtid.x < RADIX) //take advantage of barrier to grab value
            exclusiveHistReduction = g_d[gtid.x >> 1] >> ((gtid.x & 1) ? 16 : 0) & 0xffff;
        GroupMemoryBarrierWithGroupSync();
    }
    
    ScatterKeysShared(offsets, keys);
    LoadThreadBlockReductions(gtid.x, flattenGid(gid), exclusiveHistReduction);
    GroupMemoryBarrierWithGroupSync();
    
    if (flattenGid(gid) < e_threadBlocks - 1)
        ScatterDevice(gtid.x, flattenGid(gid), offsets);
        
    if (flattenGid(gid) == e_threadBlocks - 1)
        ScatterDevicePartial(gtid.x, flattenGid(gid), offsets);
}