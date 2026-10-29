#pragma once
#include "common/graphics.h"
#include "common/ipc.h"

namespace slvr {
// Incoming resources may be replaced or unavailable while the displayed frame
// stays owned by the receiver. Only a completed copy replaces that last frame.
class FrameReceiver {
    struct Incoming {
        ComPtr<ID3D11Texture2D> shared,texture;
        ComPtr<IDXGIKeyedMutex> mutex;
        ComPtr<ID3D11ShaderResourceView> view;
        uint64_t handle=0,generation=0;
    } incoming_;
    Graphics& gpu_;
    ComPtr<ID3D11Texture2D> displayed_;
    ComPtr<ID3D11ShaderResourceView> view_;
    uint64_t received_=0;
public:
    explicit FrameReceiver(Graphics& gpu):gpu_(gpu){}
    void Disconnect(){incoming_={};}
    void Reset(){Disconnect();displayed_.Reset();view_.Reset();received_=0;}
    bool HasFrame() const{return view_!=nullptr;}
    uint64_t ReceivedAt() const{return received_;}
    ID3D11Texture2D* Texture() const{return displayed_.Get();}
    ID3D11ShaderResourceView* View() const{return view_.Get();}
    void Receive(const SharedState& state){
        if(!state.driverPid||!state.frameHandle){Disconnect();return;}
        if(state.frameHandle!=incoming_.handle||state.frameGeneration!=incoming_.generation){
            Incoming next;
            Check(gpu_.device->OpenSharedResource(reinterpret_cast<HANDLE>(state.frameHandle),IID_PPV_ARGS(&next.shared)),"Open driver texture");
            Check(next.shared.As(&next.mutex),"Query frame synchronization");
            D3D11_TEXTURE2D_DESC desc{};next.shared->GetDesc(&desc);
            if(!desc.Width||!desc.Height||desc.SampleDesc.Count!=1)throw std::runtime_error("Invalid frame dimensions");
            desc.MiscFlags=0;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            DXGI_FORMAT format=desc.Format;
            if(format==DXGI_FORMAT_R8G8B8A8_TYPELESS||format==DXGI_FORMAT_R8G8B8A8_UNORM||format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB){desc.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;format=DXGI_FORMAT_R8G8B8A8_UNORM;}
            else if(format==DXGI_FORMAT_B8G8R8A8_TYPELESS||format==DXGI_FORMAT_B8G8R8A8_UNORM||format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB){desc.Format=DXGI_FORMAT_B8G8R8A8_TYPELESS;format=DXGI_FORMAT_B8G8R8A8_UNORM;}
            Check(gpu_.device->CreateTexture2D(&desc,nullptr,&next.texture),"Create retained frame");
            D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=format;view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;view.Texture2D.MipLevels=1;
            Check(gpu_.device->CreateShaderResourceView(next.texture.Get(),&view,&next.view),"Create retained frame view");
            next.handle=state.frameHandle;next.generation=state.frameGeneration;incoming_=std::move(next);
        }
        KeyedMutexGuard lock;if(!lock.Acquire(incoming_.mutex.Get(),1,0,0))return;
        ID3D11ShaderResourceView* empty=nullptr;gpu_.context->PSSetShaderResources(0,1,&empty);
        gpu_.context->CopyResource(incoming_.texture.Get(),incoming_.shared.Get());gpu_.context->Flush();lock.Release(0);
        displayed_=incoming_.texture;view_=incoming_.view;received_=GetTickCount64();
    }
};
}
