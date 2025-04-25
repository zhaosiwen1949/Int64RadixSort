/******************************************************************************
 * GPUSorting
 *
 * SPDX-License-Identifier: MIT
 * Copyright Thomas Smith 2/13/2024
 * https://github.com/b0nes164/GPUSorting
 *
 ******************************************************************************/
#include "pch.h"
#include "DeviceRadixSort.h"
#include "OneSweep.h"
#include "FFXParallelSort.h"
#include "Tests.h"

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 613; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = u8".\\D3D12\\"; }

GPUSorting::DeviceInfo GetDeviceInfo(ID3D12Device* device)
{
    GPUSorting::DeviceInfo devInfo = {};
    auto adapterLuid = device->GetAdapterLuid();
    winrt::com_ptr<IDXGIFactory4> factory;
    winrt::check_hresult(CreateDXGIFactory2(0, IID_PPV_ARGS(factory.put())));

    winrt::com_ptr<IDXGIAdapter1> adapter;
    winrt::check_hresult(factory->EnumAdapterByLuid(adapterLuid, IID_PPV_ARGS(adapter.put())));

    DXGI_ADAPTER_DESC1 adapterDesc{};
    winrt::check_hresult(adapter->GetDesc1(&adapterDesc));

    devInfo.Description = adapterDesc.Description;
    devInfo.deviceId = adapterDesc.DeviceId;
    devInfo.vendorId = adapterDesc.VendorId;
    devInfo.dedicatedVideoMemory = adapterDesc.DedicatedVideoMemory;
    devInfo.sharedSystemMemory = adapterDesc.SharedSystemMemory;

    bool isWarpDevice = ((adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == DXGI_ADAPTER_FLAG_SOFTWARE) ||
        (_wcsicmp(adapterDesc.Description, L"Microsoft Basic Render Driver") == 0);

    D3D12_FEATURE_DATA_SHADER_MODEL model{ D3D_SHADER_MODEL_6_7 };
    winrt::check_hresult(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model)));

    static wchar_t const* shaderModelName[] = { L"cs_6_0", L"cs_6_1", L"cs_6_2", L"cs_6_3", 
                                                L"cs_6_4", L"cs_6_5", L"cs_6_6", L"cs_6_7" };
    uint32_t index = model.HighestShaderModel & 0xF;
    winrt::check_hresult((index >= _countof(shaderModelName) ? E_UNEXPECTED : S_OK));
    devInfo.SupportedShaderModel = shaderModelName[index];

    D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1 = {};
    winrt::check_hresult(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1)));

    D3D12_FEATURE_DATA_D3D12_OPTIONS4 options4 = {};
    winrt::check_hresult(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS4, &options4, sizeof(options4)));

    //16 bit types nice, but not required
    //MatchAny also uneccessary
    devInfo.SIMDWidth = options1.WaveLaneCountMin;
    devInfo.SIMDMaxWidth = options1.WaveLaneCountMax;
    devInfo.SIMDLaneCount = options1.TotalLaneCount;
    devInfo.SupportsWaveIntrinsics = options1.WaveOps;
    devInfo.Supports16BitTypes = options4.Native16BitShaderOpsSupported;
    devInfo.SupportsDeviceRadixSort = ( devInfo.SIMDWidth >= 4 && devInfo.SupportsWaveIntrinsics &&
        model.HighestShaderModel >= D3D_SHADER_MODEL_6_0 );
    devInfo.SupportsOneSweep = devInfo.SupportsDeviceRadixSort && !isWarpDevice;

#ifdef _DEBUG
    std::wcout << L"Device:                  " << devInfo.Description << L"\n";
    std::wcout << L"Supported Shader Model:  " << devInfo.SupportedShaderModel << L"\n";
    std::cout << "Min wave width:            " << devInfo.SIMDWidth << "\n";
    std::cout << "Max wave width:            " << devInfo.SIMDMaxWidth << "\n";
    std::cout << "Total lanes:               " << devInfo.SIMDLaneCount << "\n";
    std::cout << "Dedicated video memory:    " << devInfo.dedicatedVideoMemory << "\n";
    std::cout << "Shared system memory       " << devInfo.sharedSystemMemory << "\n";
    std::cout << "Supports Wave Intrinsics:  " << (devInfo.SupportsWaveIntrinsics ? "Yes" : "No") << "\n";
    std::cout << "Supports 16Bit Types:      " << (devInfo.Supports16BitTypes ? "Yes" : "No") << "\n";
    std::cout << "Supports DeviceRadixSort:  " << (devInfo.SupportsDeviceRadixSort ? "Yes" : "No") << "\n";
    std::cout << "Supports OneSweep:         " << (devInfo.SupportsOneSweep ? "Yes" : "No") << "\n\n";
#endif

    return devInfo;
}

winrt::com_ptr<ID3D12Device> InitDevice()
{
#ifdef _DEBUG
    winrt::com_ptr<ID3D12Debug6> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
        debugController->EnableDebugLayer();
    }
    else
    {
        std::cerr << "WARNING: D3D12 debug interface not available" << std::endl;
    }
#endif

    winrt::com_ptr<ID3D12Device> device;
    winrt::check_hresult(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(device.put())));

#ifdef _DEBUG
    
#endif 
    return device;
}

winrt::com_ptr<ID3D12Device> InitDeviceWarp()
{
    winrt::com_ptr<ID3D12Device> device;
    winrt::com_ptr<IDXGIFactory4> factory;
    winrt::check_hresult(CreateDXGIFactory2(0, IID_PPV_ARGS(factory.put())));

    winrt::com_ptr<IDXGIAdapter1> adapter;
    winrt::check_hresult(factory->EnumWarpAdapter(IID_PPV_ARGS(adapter.put())));
    winrt::check_hresult(D3D12CreateDevice(adapter.get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(device.put())));
    return device;
}

int main()
{
    winrt::com_ptr<ID3D12Device> device = InitDevice();
    //winrt::com_ptr<ID3D12Device> device = InitDeviceWarp(); <- To test WARP, you will need NuGet package
    GPUSorting::DeviceInfo deviceInfo = GetDeviceInfo(device.get());

    DeviceRadixSort* dvr = new DeviceRadixSort(
        device, 
        deviceInfo,
        GPUSorting::ORDER_ASCENDING,
        GPUSorting::KEY_UINT32,
        GPUSorting::PAYLOAD_UINT32);
    dvr->TestAll();
    dvr->BatchTiming(1 << 28, 100, 10, GPUSorting::ENTROPY_PRESET_1);
    dvr->~DeviceRadixSort();

    OneSweep* oneSweep = new OneSweep(
        device,
        deviceInfo,
        GPUSorting::ORDER_ASCENDING,
        GPUSorting::KEY_UINT32,
        GPUSorting::PAYLOAD_UINT32);
    oneSweep->TestAll();
    oneSweep->BatchTiming(1 << 28, 100, 10, GPUSorting::ENTROPY_PRESET_1);
    oneSweep->~OneSweep();

    FFXParallelSort* ffxPs = new FFXParallelSort(
        device,
        deviceInfo,
        GPUSorting::ORDER_ASCENDING,
        GPUSorting::KEY_UINT32,
        GPUSorting::PAYLOAD_UINT32);
    ffxPs->TestAll();
    ffxPs->BatchTiming(1 << 25, 100, 10, GPUSorting::ENTROPY_PRESET_1);
    ffxPs->~FFXParallelSort();

    //SuperTestOneSweep(device, deviceInfo);            <-Test the complete feature space,
    //SuperTestDeviceRadixSort(device, deviceInfo);     <-this will take a while!

    //BenchmarkOneSweep(device, deviceInfo);            //<-Grab a cup of coffee, this may take 5 - 10 minutes              

    return 0;
}