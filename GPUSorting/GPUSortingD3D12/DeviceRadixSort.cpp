/******************************************************************************
 * GPUSorting
 *
 * SPDX-License-Identifier: MIT
 * Copyright Thomas Smith 2/13/2024
 * https://github.com/b0nes164/GPUSorting
 *
 ******************************************************************************/
#pragma once
#include "pch.h"
#include "DeviceRadixSort.h"

DeviceRadixSort::DeviceRadixSort(
    winrt::com_ptr<ID3D12Device> _device,
    GPUSorting::DeviceInfo _deviceInfo,
    GPUSorting::ORDER sortingOrder,
    GPUSorting::KEY_TYPE keyType) :
    GPUSortBase(
        _device,
        _deviceInfo,
        sortingOrder,
        keyType,
        "DeviceRadixSort ",
        4,
        256,
        1 << 13)
{
    m_device.copy_from(_device.get());
    SetCompileArguments();
    Initialize();
}

DeviceRadixSort::DeviceRadixSort(
    winrt::com_ptr<ID3D12Device> _device,
    GPUSorting::DeviceInfo _deviceInfo,
    GPUSorting::ORDER sortingOrder,
    GPUSorting::KEY_TYPE keyType,
    GPUSorting::PAYLOAD_TYPE payloadType) :
    GPUSortBase(
        _device,
        _deviceInfo,
        sortingOrder,
        keyType,
        payloadType,
        "DeviceRadixSort ",
        4,
        256,
        1 << 13)
{
    m_device.copy_from(_device.get());
    SetCompileArguments();
    Initialize();
}

DeviceRadixSort::~DeviceRadixSort()
{
}

bool DeviceRadixSort::TestAll()
{
    printf("Beginning ");
    printf(k_sortName);
    PrintSortingConfig(k_sortingConfig);
    printf("test all. \n");

    uint32_t sortPayloadTestsPassed = 0;
    uint32_t testsExpected = k_tuningParameters.partitionSize + 1 + 255 + 3;

    const uint32_t testEnd = k_tuningParameters.partitionSize * 2 + 1;
    for (uint32_t i = k_tuningParameters.partitionSize; i < testEnd; ++i)
    {
        sortPayloadTestsPassed += ValidateSort(i, i);

        if (!(i & 127))
            printf(".");
    }

    printf("\n");
    printf("%u / %u passed. \n", sortPayloadTestsPassed, k_tuningParameters.partitionSize + 1);

    UpdateSize(1 << 22); //TODO: BAD!
    printf("Beginning interthreadblock scan validation tests. \n");
    uint32_t scanTestsPassed = 0;
    for (uint32_t i = 1; i < 256; ++i)
    {
        scanTestsPassed += ValidateScan(i);
        if (!(i & 7))
            printf(".");
    }

    printf("\n");
    printf("%u / %u passed. \n", scanTestsPassed, 255);

    //Validate the multi-dispatching approach to handle large inputs.
    //This has extremely large memory requirements. So we check to make
    //sure we can do it.
    printf("Beginning large size tests\n");
    sortPayloadTestsPassed += ValidateSort(1 << 21, 5);
    sortPayloadTestsPassed += ValidateSort(1 << 22, 7);
    sortPayloadTestsPassed += ValidateSort(1 << 23, 11);

    uint64_t totalAvailableMemory = m_devInfo.dedicatedVideoMemory + m_devInfo.sharedSystemMemory;
    uint64_t maxDimTestSize = k_maxDispatchDimension * k_tuningParameters.partitionSize;

    uint64_t staticMemoryRequirements =
        (k_radix * k_radixPasses * sizeof(uint32_t)) +      //This is the global histogram
        (sizeof(uint32_t)) +                                //The error buffer
        k_maxReadBack * sizeof(uint32_t);                   //The readback buffer

    //Multiply by 4 for sort, payload, alt, alt payload, add 1
    //in case fragmentation of the memory causes issues when spilling into shared system memory. 
    uint64_t pairsMemoryRequirements =
        (k_maxDispatchDimension * k_tuningParameters.partitionSize * sizeof(uint32_t) * 5) +
        staticMemoryRequirements +
        ((1 << 20) * sizeof(uint32_t));

    if (totalAvailableMemory >= pairsMemoryRequirements)
    {
        sortPayloadTestsPassed += ValidateSort(maxDimTestSize - 1, 13);
        sortPayloadTestsPassed += ValidateSort(maxDimTestSize, 17);
        sortPayloadTestsPassed += ValidateSort(maxDimTestSize + (1 << 20), 19);
        testsExpected += 3;
    }
    else
    {
        printf("Warning, device does not have enough memory to test multi-dispatch");
        printf(" handling of very large inputs. These tests have been skipped\n");
    }
    
    if (sortPayloadTestsPassed + scanTestsPassed == testsExpected)
    {
        printf("%u / %u  All tests passed. \n\n", testsExpected, testsExpected);
        return true;
    }
    else
    {
        printf("%u / %u  Test failed. \n\n", sortPayloadTestsPassed + scanTestsPassed, testsExpected);
        return false;
    }
}

void DeviceRadixSort::InitUtilityComputeShaders()
{
    const std::filesystem::path path = "Shaders/Utility.hlsl";
    m_initSortInput = new UtilityKernels::InitSortInput(m_device, m_devInfo, m_compileArguments, path);
    m_clearErrorCount = new UtilityKernels::ClearErrorCount(m_device, m_devInfo, m_compileArguments, path);
    m_validate = new UtilityKernels::Validate(m_device, m_devInfo, m_compileArguments, path);
    m_initScanTestValues = new UtilityKernels::InitScanTestValues(m_device, m_devInfo, m_compileArguments, path);
}

void DeviceRadixSort::InitComputeShaders()
{
    const std::filesystem::path path = "Shaders/DeviceRadixSort.hlsl";
    m_initDeviceRadix = new DeviceRadixSortKernels::InitDeviceRadixSort(m_device, m_devInfo, m_compileArguments, path);
    m_upsweep = new DeviceRadixSortKernels::Upsweep(m_device, m_devInfo, m_compileArguments, path);
    m_scan = new DeviceRadixSortKernels::Scan(m_device, m_devInfo, m_compileArguments, path);
    m_downsweep = new DeviceRadixSortKernels::Downsweep(m_device, m_devInfo, m_compileArguments, path);
}

void DeviceRadixSort::UpdateSize(uint32_t size)
{
    if (m_numKeys != size)
    {
        m_numKeys = size;
        m_partitions = divRoundUp(m_numKeys, k_tuningParameters.partitionSize);
        DisposeBuffers();
        InitBuffers(m_numKeys, m_partitions);
    }
}

void DeviceRadixSort::DisposeBuffers()
{
    m_sortBuffer = nullptr;
    m_sortPayloadBuffer = nullptr;
    m_altBuffer = nullptr;
    m_altPayloadBuffer = nullptr;
    m_passHistBuffer = nullptr;
}

void DeviceRadixSort::InitStaticBuffers()
{
    m_globalHistBuffer = CreateBuffer(
        m_device,
        k_radix * k_radixPasses * sizeof(uint32_t),
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    m_errorCountBuffer = CreateBuffer(
        m_device,
        1 * sizeof(uint32_t),
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    m_readBackBuffer = CreateBuffer(
        m_device,
        k_maxReadBack * sizeof(uint32_t),
        D3D12_HEAP_TYPE_READBACK,
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_FLAG_NONE);
}

void DeviceRadixSort::InitBuffers(const uint32_t numKeys, const uint32_t threadBlocks)
{
    m_sortBuffer = CreateBuffer(
        m_device,
        numKeys * sizeof(uint32_t),
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    m_altBuffer = CreateBuffer(
        m_device,
        numKeys * sizeof(uint32_t),
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    m_passHistBuffer = CreateBuffer(
        m_device,
        k_radix * threadBlocks * sizeof(uint32_t),
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    if (k_sortingConfig.sortingMode == GPUSorting::MODE_PAIRS)
    {
        m_sortPayloadBuffer = CreateBuffer(
            m_device,
            numKeys * sizeof(uint32_t),
            D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        m_altPayloadBuffer = CreateBuffer(
            m_device,
            numKeys * sizeof(uint32_t),
            D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    }
    else
    {
        m_sortPayloadBuffer = CreateBuffer(
            m_device,
            1 * sizeof(uint32_t),
            D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        m_altPayloadBuffer = CreateBuffer(
            m_device,
            1 * sizeof(uint32_t),
            D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    }
}

void DeviceRadixSort::PrepareSortCmdList()
{
    m_initDeviceRadix->Dispatch(
        m_cmdList,
        m_globalHistBuffer->GetGPUVirtualAddress());
    UAVBarrierSingle(m_cmdList, m_globalHistBuffer);

    for (uint32_t radixShift = 0; radixShift < 32; radixShift += 8)
    {
        m_upsweep->Dispatch(
            m_cmdList,
            m_sortBuffer->GetGPUVirtualAddress(),
            m_globalHistBuffer->GetGPUVirtualAddress(),
            m_passHistBuffer->GetGPUVirtualAddress(),
            m_numKeys,
            m_partitions,
            radixShift);
        UAVBarrierSingle(m_cmdList, m_passHistBuffer);

        m_scan->Dispatch(
            m_cmdList,
            m_passHistBuffer->GetGPUVirtualAddress(),
            m_partitions);
        UAVBarrierSingle(m_cmdList, m_passHistBuffer);
        UAVBarrierSingle(m_cmdList, m_globalHistBuffer);

        m_downsweep->Dispatch(
            m_cmdList,
            m_sortBuffer->GetGPUVirtualAddress(),
            m_sortPayloadBuffer->GetGPUVirtualAddress(),
            m_altBuffer->GetGPUVirtualAddress(),
            m_altPayloadBuffer->GetGPUVirtualAddress(),
            m_globalHistBuffer->GetGPUVirtualAddress(),
            m_passHistBuffer->GetGPUVirtualAddress(),
            m_numKeys,
            m_partitions,
            radixShift);
        UAVBarrierSingle(m_cmdList, m_sortBuffer);
        UAVBarrierSingle(m_cmdList, m_sortPayloadBuffer);
        UAVBarrierSingle(m_cmdList, m_altBuffer);
        UAVBarrierSingle(m_cmdList, m_altPayloadBuffer);

        swap(m_sortBuffer, m_altBuffer);
        swap(m_sortPayloadBuffer, m_altPayloadBuffer);
    }
}

bool DeviceRadixSort::ValidateScan(uint32_t size)
{
    m_initScanTestValues->Dispatch(
        m_cmdList,
        m_passHistBuffer->GetGPUVirtualAddress(),
        size);
    UAVBarrierSingle(m_cmdList, m_passHistBuffer);

    m_scan->Dispatch(
        m_cmdList,
        m_passHistBuffer->GetGPUVirtualAddress(),
        size);
    ExecuteCommandList();

    m_cmdList->CopyBufferRegion(m_readBackBuffer.get(), 0, m_passHistBuffer.get(), 0, (uint64_t)size * sizeof(uint32_t));
    ExecuteCommandList();

    std::vector<uint32_t> vecOut = ReadBackBuffer(m_readBackBuffer, size);

    bool isValid = true;
    for (uint32_t i = 0; i < size; ++i)
    {
        if (vecOut[i] != i)
        {
            printf("\nFailed at size %u.\n", size);
            isValid = false;

            break;
        }
    }

    return isValid;
}
