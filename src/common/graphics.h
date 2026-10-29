#pragma once
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <stdexcept>
#include <string>

namespace slvr {
using Microsoft::WRL::ComPtr;
inline void Check(HRESULT hr, const char* what) {
    if (FAILED(hr)) throw std::runtime_error(std::string(what)+" (HRESULT "+std::to_string(static_cast<unsigned long>(hr))+")");
}
inline bool SameLuid(LUID a,LUID b) {return a.LowPart==b.LowPart && a.HighPart==b.HighPart;}
class KeyedMutexGuard {
    IDXGIKeyedMutex* mutex_=nullptr;
    uint64_t fallbackKey_=0;
public:
    KeyedMutexGuard()=default;
    KeyedMutexGuard(const KeyedMutexGuard&)=delete;
    KeyedMutexGuard& operator=(const KeyedMutexGuard&)=delete;
    ~KeyedMutexGuard() {if(mutex_)mutex_->ReleaseSync(fallbackKey_);}
    bool Acquire(IDXGIKeyedMutex* mutex,uint64_t key,DWORD timeout,uint64_t fallbackKey) {
        if(!mutex)return true;
        const HRESULT result=mutex->AcquireSync(key,timeout);
        if(result==WAIT_TIMEOUT)return false;
        if(result!=S_OK)throw std::runtime_error("Shared texture synchronization failed");
        mutex_=mutex;fallbackKey_=fallbackKey;return true;
    }
    void Release(uint64_t key) {
        if(!mutex_)return;
        auto* mutex=mutex_;mutex_=nullptr;
        Check(mutex->ReleaseSync(key),"Release shared texture synchronization");
    }
};
struct Graphics {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    LUID luid{};
    void Open(const LUID* desired = nullptr) {
        context.Reset(); device.Reset();
        ComPtr<IDXGIFactory1> factory;
        Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"CreateDXGIFactory1");
        ComPtr<IDXGIAdapter1> selected;
        for (UINT i=0;;++i) {
            ComPtr<IDXGIAdapter1> adapter;
            if (factory->EnumAdapters1(i,&adapter)==DXGI_ERROR_NOT_FOUND) break;
            DXGI_ADAPTER_DESC1 desc{};
            Check(adapter->GetDesc1(&desc),"GetDesc1");
            if ((desired && SameLuid(desc.AdapterLuid,*desired)) ||
                (!desired && !(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE))) {
                selected=adapter; luid=desc.AdapterLuid; break;
            }
        }
        if (!selected) throw std::runtime_error("Required GPU not found");
        const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
        HRESULT hr=D3D11CreateDevice(selected.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,2,D3D11_SDK_VERSION,&device,nullptr,&context);
        if (hr==E_INVALIDARG) hr=D3D11CreateDevice(selected.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels+1,1,D3D11_SDK_VERSION,&device,nullptr,&context);
        Check(hr,"D3D11CreateDevice");
    }
};
}
