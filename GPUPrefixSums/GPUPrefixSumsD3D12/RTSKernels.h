/******************************************************************************
 * GPUPrefixSums
 *
 * SPDX-License-Identifier: MIT
 * Copyright Thomas Smith 12/2/2024
 * https://github.com/b0nes164/GPUPrefixSums
 *
 ******************************************************************************/
#pragma once
#include "pch.h"
#include "ComputeKernelBase.h"

namespace RTSKernels {
    enum class Reg {
        ScanIn = 0,
        ScanOut = 1,
        ThreadBlockReduction = 2,
    };

    class Reduce : ComputeKernelBase {
       public:
        Reduce(winrt::com_ptr<ID3D12Device> device, const GPUPrefixSums::DeviceInfo& info,
               const std::vector<std::wstring>& compileArguments,
               const std::filesystem::path& shaderPath)
            : ComputeKernelBase(device, info, shaderPath, L"Reduce", compileArguments,
                                CreateRootParameters()) {}

        void Dispatch(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList,
                      const D3D12_GPU_VIRTUAL_ADDRESS& scanInBuffer,
                      const D3D12_GPU_VIRTUAL_ADDRESS& threadBlockReductionBuffer,
                      const uint32_t& vectorizedSize, const uint32_t& threadBlocks) {
            const uint32_t fullBlocks = threadBlocks / k_maxDim;
            if (fullBlocks) {
                std::array<uint32_t, 4> t = {vectorizedSize, threadBlocks, k_isNotPartialBitFlag,
                                             0};

                SetPipelineState(cmdList);
                cmdList->SetComputeRoot32BitConstants(0, 4, t.data(), 0);
                cmdList->SetComputeRootUnorderedAccessView(1, scanInBuffer);
                cmdList->SetComputeRootUnorderedAccessView(2, threadBlockReductionBuffer);
                cmdList->Dispatch(k_maxDim, fullBlocks, 1);
            }

            const uint32_t partialBlocks = threadBlocks - fullBlocks * k_maxDim;
            if (partialBlocks) {
                std::array<uint32_t, 4> t = {vectorizedSize, threadBlocks, k_isPartialBitFlag,
                                             fullBlocks};

                SetPipelineState(cmdList);
                cmdList->SetComputeRoot32BitConstants(0, 4, t.data(), 0);
                cmdList->SetComputeRootUnorderedAccessView(1, scanInBuffer);
                cmdList->SetComputeRootUnorderedAccessView(2, threadBlockReductionBuffer);
                cmdList->Dispatch(partialBlocks, 1, 1);
            }
        }

       protected:
        const std::vector<CD3DX12_ROOT_PARAMETER1> CreateRootParameters() override {
            auto rootParams = std::vector<CD3DX12_ROOT_PARAMETER1>(3);
            rootParams[0].InitAsConstants(4, 0);
            rootParams[1].InitAsUnorderedAccessView((UINT)Reg::ScanIn);
            rootParams[2].InitAsUnorderedAccessView((UINT)Reg::ThreadBlockReduction);
            return rootParams;
        }
    };

    class Scan : public ComputeKernelBase {
       public:
        Scan(winrt::com_ptr<ID3D12Device> device, const GPUPrefixSums::DeviceInfo& info,
             const std::vector<std::wstring>& compileArguments,
             const std::filesystem::path& shaderPath)
            : ComputeKernelBase(device, info, shaderPath, L"Scan", compileArguments,
                                CreateRootParameters()) {}

        void Dispatch(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList,
                      const D3D12_GPU_VIRTUAL_ADDRESS& threadBlockReductionBuffer,
                      const uint32_t& threadBlocks) {
            std::array<uint32_t, 4> t = {0, threadBlocks, 0, 0};
            SetPipelineState(cmdList);
            cmdList->SetComputeRoot32BitConstants(0, 4, t.data(), 0);
            cmdList->SetComputeRootUnorderedAccessView(1, threadBlockReductionBuffer);
            cmdList->Dispatch(1, 1, 1);
        }

       protected:
        const std::vector<CD3DX12_ROOT_PARAMETER1> CreateRootParameters() override {
            auto rootParameters = std::vector<CD3DX12_ROOT_PARAMETER1>(2);
            rootParameters[0].InitAsConstants(4, 0);
            rootParameters[1].InitAsUnorderedAccessView((UINT)Reg::ThreadBlockReduction);
            return rootParameters;
        }
    };

    class PropagateInclusive : ComputeKernelBase {
       public:
        PropagateInclusive(winrt::com_ptr<ID3D12Device> device,
                           const GPUPrefixSums::DeviceInfo& info,
                           const std::vector<std::wstring>& compileArguments,
                           const std::filesystem::path& shaderPath)
            : ComputeKernelBase(device, info, shaderPath, L"PropagateInclusive", compileArguments,
                                CreateRootParameters()) {}

        void Dispatch(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList,
                      const D3D12_GPU_VIRTUAL_ADDRESS& scanInBuffer,
                      const D3D12_GPU_VIRTUAL_ADDRESS& scanOutBuffer,
                      const D3D12_GPU_VIRTUAL_ADDRESS& threadBlockReductionBuffer,
                      const uint32_t& vectorizedSize, const uint32_t& threadBlocks) {
            const uint32_t fullBlocks = threadBlocks / k_maxDim;
            if (fullBlocks) {
                std::array<uint32_t, 4> t = {vectorizedSize, threadBlocks, k_isNotPartialBitFlag,
                                             0};

                SetPipelineState(cmdList);
                cmdList->SetComputeRoot32BitConstants(0, 4, t.data(), 0);
                cmdList->SetComputeRootUnorderedAccessView(1, scanInBuffer);
                cmdList->SetComputeRootUnorderedAccessView(2, scanOutBuffer);
                cmdList->SetComputeRootUnorderedAccessView(3, threadBlockReductionBuffer);
                cmdList->Dispatch(k_maxDim, fullBlocks, 1);
            }

            const uint32_t partialBlocks = threadBlocks - fullBlocks * k_maxDim;
            if (partialBlocks) {
                std::array<uint32_t, 4> t = {vectorizedSize, threadBlocks, k_isPartialBitFlag,
                                             fullBlocks};
                SetPipelineState(cmdList);
                cmdList->SetComputeRoot32BitConstants(0, 4, t.data(), 0);
                cmdList->SetComputeRootUnorderedAccessView(1, scanInBuffer);
                cmdList->SetComputeRootUnorderedAccessView(2, scanOutBuffer);
                cmdList->SetComputeRootUnorderedAccessView(3, threadBlockReductionBuffer);
                cmdList->Dispatch(partialBlocks, 1, 1);
            }
        }

       protected:
        const std::vector<CD3DX12_ROOT_PARAMETER1> CreateRootParameters() override {
            auto rootParameters = std::vector<CD3DX12_ROOT_PARAMETER1>(4);
            rootParameters[0].InitAsConstants(4, 0);
            rootParameters[1].InitAsUnorderedAccessView((UINT)Reg::ScanIn);
            rootParameters[2].InitAsUnorderedAccessView((UINT)Reg::ScanOut);
            rootParameters[3].InitAsUnorderedAccessView((UINT)Reg::ThreadBlockReduction);
            return rootParameters;
        }
    };

    class PropagateExclusive : ComputeKernelBase {
       public:
        PropagateExclusive(winrt::com_ptr<ID3D12Device> device,
                           const GPUPrefixSums::DeviceInfo& info,
                           const std::vector<std::wstring>& compileArguments,
                           const std::filesystem::path& shaderPath)
            : ComputeKernelBase(device, info, shaderPath, L"PropagateExclusive", compileArguments,
                                CreateRootParameters()) {}

        void Dispatch(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList,
                      const D3D12_GPU_VIRTUAL_ADDRESS& scanInBuffer,
                      const D3D12_GPU_VIRTUAL_ADDRESS& scanOutBuffer,
                      const D3D12_GPU_VIRTUAL_ADDRESS& threadBlockReductionBuffer,
                      const uint32_t& vectorizedSize, const uint32_t& threadBlocks) {
            const uint32_t fullBlocks = threadBlocks / k_maxDim;
            if (fullBlocks) {
                std::array<uint32_t, 4> t = {vectorizedSize, threadBlocks, k_isNotPartialBitFlag,
                                             0};

                SetPipelineState(cmdList);
                cmdList->SetComputeRoot32BitConstants(0, 4, t.data(), 0);
                cmdList->SetComputeRootUnorderedAccessView(1, scanInBuffer);
                cmdList->SetComputeRootUnorderedAccessView(2, scanOutBuffer);
                cmdList->SetComputeRootUnorderedAccessView(3, threadBlockReductionBuffer);
                cmdList->Dispatch(k_maxDim, fullBlocks, 1);
            }

            const uint32_t partialBlocks = threadBlocks - fullBlocks * k_maxDim;
            if (partialBlocks) {
                std::array<uint32_t, 4> t = {vectorizedSize, threadBlocks, k_isPartialBitFlag,
                                             fullBlocks};
                SetPipelineState(cmdList);
                cmdList->SetComputeRoot32BitConstants(0, 4, t.data(), 0);
                cmdList->SetComputeRootUnorderedAccessView(1, scanInBuffer);
                cmdList->SetComputeRootUnorderedAccessView(2, scanOutBuffer);
                cmdList->SetComputeRootUnorderedAccessView(3, threadBlockReductionBuffer);
                cmdList->Dispatch(partialBlocks, 1, 1);
            }
        }

       protected:
        const std::vector<CD3DX12_ROOT_PARAMETER1> CreateRootParameters() override {
            auto rootParameters = std::vector<CD3DX12_ROOT_PARAMETER1>(4);
            rootParameters[0].InitAsConstants(4, 0);
            rootParameters[1].InitAsUnorderedAccessView((UINT)Reg::ScanIn);
            rootParameters[2].InitAsUnorderedAccessView((UINT)Reg::ScanOut);
            rootParameters[3].InitAsUnorderedAccessView((UINT)Reg::ThreadBlockReduction);
            return rootParameters;
        }
    };
}  // namespace RTSKernels