/******************************************************************************
 * GPUPrefixSums
 *
 * SPDX-License-Identifier: MIT
 * Copyright Thomas Smith 12/2/2024
 * https://github.com/b0nes164/GPUPrefixSums
 *
 ******************************************************************************/
#include "pch.h"
#include "ChainedScanDecoupledLookback.h"

ChainedScanDecoupledLookback::ChainedScanDecoupledLookback(winrt::com_ptr<ID3D12Device> _device,
                                                           GPUPrefixSums::DeviceInfo _deviceInfo)
    : GPUPrefixSumBase("ChainedScanDecoupledLookback ", 3072, 1 << 13) {
    m_device.copy_from(_device.get());
    m_devInfo = _deviceInfo;

    Initialize();
}

ChainedScanDecoupledLookback::~ChainedScanDecoupledLookback() {}

void ChainedScanDecoupledLookback::InitComputeShaders() {
    const std::filesystem::path path = "Shaders/ChainedScanDecoupledLookback.hlsl";
    m_initCSDL = new CSDLKernels::InitCSDL(m_device, m_devInfo, m_compileArguments, path);
    m_csdlInclusive = new CSDLKernels::CSDLInclusive(m_device, m_devInfo, m_compileArguments, path);
    m_csdlExclusive = new CSDLKernels::CSDLExclusive(m_device, m_devInfo, m_compileArguments, path);
}

void ChainedScanDecoupledLookback::DisposeBuffers() {
    m_scanInBuffer = nullptr;
    m_scanOutBuffer = nullptr;
    m_threadBlockReductionBuffer = nullptr;
    m_scanValidationBuffer = nullptr;
}

void ChainedScanDecoupledLookback::InitStaticBuffers() {
    m_scanBumpBuffer =
        CreateBuffer(m_device, sizeof(uint32_t), D3D12_HEAP_TYPE_DEFAULT,
                     D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    m_errorCountBuffer =
        CreateBuffer(m_device, sizeof(uint32_t), D3D12_HEAP_TYPE_DEFAULT,
                     D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    m_readBackBuffer =
        CreateBuffer(m_device, k_maxReadBack * sizeof(uint32_t), D3D12_HEAP_TYPE_READBACK,
                     D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE);
}

void ChainedScanDecoupledLookback::PrepareScanCmdListInclusive() {
    m_initCSDL->Dispatch(m_cmdList, m_scanBumpBuffer->GetGPUVirtualAddress(),
                         m_threadBlockReductionBuffer->GetGPUVirtualAddress(), m_partitions);
    UAVBarrierSingle(m_cmdList, m_scanBumpBuffer);
    UAVBarrierSingle(m_cmdList, m_threadBlockReductionBuffer);

    m_csdlInclusive->Dispatch(m_cmdList, m_scanInBuffer->GetGPUVirtualAddress(),
                              m_scanOutBuffer->GetGPUVirtualAddress(),
                              m_scanBumpBuffer->GetGPUVirtualAddress(),
                              m_threadBlockReductionBuffer, m_vectorizedSize, m_partitions);
}

void ChainedScanDecoupledLookback::PrepareScanCmdListExclusive() {
    m_initCSDL->Dispatch(m_cmdList, m_scanBumpBuffer->GetGPUVirtualAddress(),
                         m_threadBlockReductionBuffer->GetGPUVirtualAddress(), m_partitions);
    UAVBarrierSingle(m_cmdList, m_scanBumpBuffer);
    UAVBarrierSingle(m_cmdList, m_threadBlockReductionBuffer);

    m_csdlExclusive->Dispatch(m_cmdList, m_scanInBuffer->GetGPUVirtualAddress(),
                              m_scanOutBuffer->GetGPUVirtualAddress(),
                              m_scanBumpBuffer->GetGPUVirtualAddress(),
                              m_threadBlockReductionBuffer, m_vectorizedSize, m_partitions);
}