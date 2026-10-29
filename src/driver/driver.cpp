#include "openvr_driver.h"
#include "common/graphics.h"
#include "common/ipc.h"
#include "platform/settings.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>

namespace {
using namespace slvr;
constexpr const char* Settings = "driver_freelinkvr";

class Headset final : public vr::ITrackedDeviceServerDriver,
                      public vr::IVRDisplayComponent,
                      public vr::IVRVirtualDisplay {
    SharedMemory& memory_;
    SharedState poseState_{};
    std::mutex poseLock_;
    Graphics gpu_;
    ComPtr<ID3D11Texture2D> source_, output_;
    ComPtr<IDXGIKeyedMutex> sourceMutex_;
    ComPtr<IDXGIKeyedMutex> outputMutex_;
    ComPtr<ID3D11Query> copyFinished_;
    bool copyPending_=false;
    uint64_t sourceHandle_ = 0;
    HANDLE outputHandle_ = nullptr;
    std::mutex graphicsLock_;
    std::atomic<uint32_t> index_{vr::k_unTrackedDeviceIndexInvalid};
    uint32_t width_=1920,height_=1080;
    float hz_=60,ipd_=0.064f,fov_=90,heightMeters_=1.65f,k1_=0,k2_=0;
    std::chrono::steady_clock::time_point epoch_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point presentDeadline_=epoch_;
    std::atomic<bool> graphicsError_{false};
    std::atomic<bool> presentSeen_{false};

    float ReadFloat(const char* key,float fallback,float minimum,float maximum) {
        vr::EVRSettingsError error=vr::VRSettingsError_None;
        float value=vr::VRSettings()->GetFloat(Settings,key,&error);
        return error==vr::VRSettingsError_None && std::isfinite(value) ? std::clamp(value,minimum,maximum) : fallback;
    }
    uint32_t ReadSize(const char* key,int fallback,int minimum,int maximum) {
        vr::EVRSettingsError error=vr::VRSettingsError_None;
        int value=vr::VRSettings()->GetInt32(Settings,key,&error);
        return static_cast<uint32_t>(std::clamp(error==vr::VRSettingsError_None ? value : fallback,minimum,maximum));
    }
    void PublishError(const char* message) {
        if (!graphicsError_.exchange(true)) vr::VRDriverLog()->Log(message);
        memory_.WithLock([](SharedState& s){s.status=DriverStatus::GraphicsError;});
    }
public:
    explicit Headset(SharedMemory& memory):memory_(memory) {}
    vr::EVRInitError Activate(uint32_t index) override {
        try {
            const auto user=UserSettings::Load();
            width_=static_cast<uint32_t>(user.width);height_=static_cast<uint32_t>(user.height);
            hz_=static_cast<float>(user.fps);ipd_=user.ipdMm/1000.0f;
            fov_=ReadFloat("fovDegrees",90,50,120);
            heightMeters_=ReadFloat("seatedHeightMeters",1.65f,0,3);
            // Optical correction is applied once in the desktop viewer.
            k1_=k2_=0;
            gpu_.Open();
            D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};
            Check(gpu_.device->CreateQuery(&query,&copyFinished_),"Create GPU completion query");
            auto properties=vr::VRProperties();
            auto container=properties->TrackedDeviceToPropertyContainer(index);
            properties->SetStringProperty(container,vr::Prop_TrackingSystemName_String,"freelinkvr");
            properties->SetStringProperty(container,vr::Prop_ModelNumber_String,"FreeLinkVR Virtual HMD");
            properties->SetStringProperty(container,vr::Prop_SerialNumber_String,"SLVR-HMD-001");
            properties->SetFloatProperty(container,vr::Prop_DisplayFrequency_Float,hz_);
            properties->SetFloatProperty(container,vr::Prop_SecondsFromVsyncToPhotons_Float,0.015f);
            properties->SetFloatProperty(container,vr::Prop_UserIpdMeters_Float,ipd_);
            properties->SetUint64Property(container,vr::Prop_CurrentUniverseId_Uint64,42);
            uint64_t adapterId=0;
            static_assert(sizeof(adapterId)==sizeof(gpu_.luid));
            std::memcpy(&adapterId,&gpu_.luid,sizeof(adapterId));
            properties->SetUint64Property(container,vr::Prop_GraphicsAdapterLuid_Uint64,adapterId);
            properties->SetBoolProperty(container,vr::Prop_IsOnDesktop_Bool,false);
            properties->SetBoolProperty(container,vr::Prop_DeviceIsWireless_Bool,true);
            properties->SetBoolProperty(container,vr::Prop_ContainsProximitySensor_Bool,false);
            properties->SetBoolProperty(container,vr::Prop_DeviceCanPowerOff_Bool,false);
            properties->SetBoolProperty(container,vr::Prop_HasDisplayComponent_Bool,true);
            properties->SetBoolProperty(container,vr::Prop_HasVirtualDisplayComponent_Bool,true);
            properties->SetBoolProperty(container,vr::Prop_HasDriverDirectModeComponent_Bool,false);
            epoch_=std::chrono::steady_clock::now();
            index_=index;
            memory_.WithLock([&](SharedState& s){
                s.driverPid=GetCurrentProcessId(); s.status=DriverStatus::Ready;
                s.frameHandle=0; s.frames=0; s.frameTimeMs=0; s.adapterLuid=gpu_.luid;
            },100);
            vr::VRDriverLog()->Log("FreeLinkVR activated (virtual display, fixed-position 3DoF).");
            return vr::VRInitError_None;
        } catch (const std::exception& e) {
            PublishError(e.what());
            return vr::VRInitError_Driver_Failed;
        }
    }
    void Deactivate() override {
        index_=vr::k_unTrackedDeviceIndexInvalid;
        std::lock_guard<std::mutex> guard(graphicsLock_);
        memory_.WithLock([](SharedState& s){
            s.driverPid=0; s.frameHandle=0; s.status=DriverStatus::Offline; ++s.frameGeneration;
        },100);
        copyFinished_.Reset(); copyPending_=false;
        outputMutex_.Reset(); output_.Reset(); sourceMutex_.Reset(); source_.Reset();
        gpu_.context.Reset(); gpu_.device.Reset();
        sourceHandle_=0; outputHandle_=nullptr;
    }
    void EnterStandby() override {}
    void* GetComponent(const char* version) override {
        if (!version) return nullptr;
        if (!std::strcmp(version,vr::IVRDisplayComponent_Version)) return static_cast<vr::IVRDisplayComponent*>(this);
        if (!std::strcmp(version,vr::IVRVirtualDisplay_Version)) return static_cast<vr::IVRVirtualDisplay*>(this);
        return nullptr;
    }
    void DebugRequest(const char*,char* response,uint32_t size) override {
        if (response && size) response[0]=0;
    }
    vr::DriverPose_t GetPose() override {
        vr::DriverPose_t pose{};
        pose.qWorldFromDriverRotation.w=1;
        pose.qDriverFromHeadRotation.w=1;
        pose.qRotation.w=1;
        pose.vecPosition[1]=heightMeters_;
        pose.deviceIsConnected=true;
        pose.willDriftInYaw=true;
        pose.shouldApplyHeadModel=false;
        // IPC contention does not mean tracking disappeared. Retain the last
        // observed pose only until its original freshness deadline.
        std::lock_guard<std::mutex> poseGuard(poseLock_);
        SharedState state{};
        if(memory_.Snapshot(state))poseState_=state;
        if (PoseFresh(poseState_,GetTickCount64())) {
            Quaternion q=poseState_.orientation;
            if (Normalize(q)) {
                pose.qRotation={q.w,q.x,q.y,q.z};
                pose.poseTimeOffset=-static_cast<double>(GetTickCount64()-poseState_.poseTimeMs)/1000;
                pose.poseIsValid=true;
                pose.result=vr::TrackingResult_Running_OK;
                return pose;
            }
        }
        pose.result=vr::TrackingResult_Running_OutOfRange;
        return pose;
    }
    void RunFrame() {
        const auto index=index_.load();
        if (index!=vr::k_unTrackedDeviceIndexInvalid)
            vr::VRServerDriverHost()->TrackedDevicePoseUpdated(index,GetPose(),sizeof(vr::DriverPose_t));
    }
    void GetWindowBounds(int32_t* x,int32_t* y,uint32_t* w,uint32_t* h) override {
        *x=0; *y=0; *w=width_; *h=height_;
    }
    bool IsDisplayOnDesktop() override {return false;}
    bool IsDisplayRealDisplay() override {return false;}
    void GetRecommendedRenderTargetSize(uint32_t* w,uint32_t* h) override {
        *w=width_/2; *h=height_;
    }
    void GetEyeOutputViewport(vr::EVREye eye,uint32_t* x,uint32_t* y,uint32_t* w,uint32_t* h) override {
        *x=eye==vr::Eye_Left ? 0 : width_/2; *y=0; *w=width_/2; *h=height_;
    }
    void GetProjectionRaw(vr::EVREye,float* left,float* right,float* top,float* bottom) override {
        const float horizontal=std::tan(fov_*static_cast<float>(Pi)/360);
        const float vertical=horizontal*static_cast<float>(height_)/(static_cast<float>(width_)/2);
        *left=-horizontal; *right=horizontal; *top=-vertical; *bottom=vertical;
    }
    vr::DistortionCoordinates_t ComputeDistortion(vr::EVREye,float u,float v) override {
        float outU=0,outV=0;
        Distort(u,v,k1_,k2_,outU,outV);
        vr::DistortionCoordinates_t result{};
        result.rfRed[0]=result.rfGreen[0]=result.rfBlue[0]=outU;
        result.rfRed[1]=result.rfGreen[1]=result.rfBlue[1]=outV;
        return result;
    }
    bool ComputeInverseDistortion(vr::HmdVector2_t* result,vr::EVREye,uint32_t,float u,float v) override {
        if (!result || !std::isfinite(u) || !std::isfinite(v)) return false;
        const float tx=2*u-1,ty=2*v-1,target=std::sqrt(tx*tx+ty*ty);
        float radius=target;
        for (int i=0;i<16;++i) {
            const float r2=radius*radius;
            const float slope=1+3*k1_*r2+5*k2_*r2*r2;
            if (slope<0.01f) return false;
            radius=std::max(0.0f,radius-(radius*(1+k1_*r2+k2_*r2*r2)-target)/slope);
        }
        const float scale=target>1e-8f ? radius/target : 1;
        result->v[0]=(tx*scale+1)/2; result->v[1]=(ty*scale+1)/2;
        float checkU=0,checkV=0;
        Distort(result->v[0],result->v[1],k1_,k2_,checkU,checkV);
        return std::isfinite(checkU) && std::isfinite(checkV) &&
               std::abs(checkU-u)<1e-4f && std::abs(checkV-v)<1e-4f;
    }
    void Present(const vr::PresentInfo_t* info,uint32_t size) override {
        if (!presentSeen_.exchange(true)) vr::VRDriverLog()->Log("FreeLinkVR: first virtual-display Present callback.");
        if (!info || size<sizeof(*info) || !info->backbufferTextureHandle) return;
        std::lock_guard<std::mutex> guard(graphicsLock_);
        if (!gpu_.device) return;
        // Fix the deadline before GPU work; recomputing it afterwards can add a
        // second refresh interval when the copy finishes just past a boundary.
        presentDeadline_=NextVsync(epoch_,std::chrono::steady_clock::now(),hz_);
        try {
            if (sourceHandle_!=info->backbufferTextureHandle || !source_) {
                sourceMutex_.Reset(); source_.Reset();
                Check(gpu_.device->OpenSharedResource(reinterpret_cast<HANDLE>(info->backbufferTextureHandle),
                    IID_PPV_ARGS(&source_)),"Open compositor texture");
                sourceHandle_=info->backbufferTextureHandle;
                // SteamVR's backbuffer is keyed. The callback alone is not a GPU
                // ordering barrier; acquire/release key 0 as specified by Valve.
                source_.As(&sourceMutex_); // Non-keyed textures also work in diagnostics.
            }
            D3D11_TEXTURE2D_DESC desc{}; source_->GetDesc(&desc);
            D3D11_TEXTURE2D_DESC previous{}; if (output_) output_->GetDesc(&previous);
            if (!output_ || desc.Width!=previous.Width || desc.Height!=previous.Height || desc.Format!=previous.Format) {
                outputMutex_.Reset(); output_.Reset();
                if (desc.SampleDesc.Count!=1) throw std::runtime_error("Multisample compositor output is unsupported");
                desc.Usage=D3D11_USAGE_DEFAULT; desc.CPUAccessFlags=0;
                desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
                Check(gpu_.device->CreateTexture2D(&desc,nullptr,&output_),"Create shared output");
                Check(output_.As(&outputMutex_),"Query keyed mutex");
                ComPtr<IDXGIResource> resource; Check(output_.As(&resource),"Query shared output");
                Check(resource->GetSharedHandle(&outputHandle_),"Get shared output handle");
                if (!memory_.WithLock([&](SharedState& s){
                    s.frameHandle=reinterpret_cast<uint64_t>(outputHandle_);
                    ++s.frameGeneration; s.width=desc.Width; s.height=desc.Height;
                    s.adapterLuid=gpu_.luid;
                },10)) { output_.Reset(); outputMutex_.Reset(); return; }
            }
            // Producer key 0 -> consumer key 1. Never block the compositor for a slow viewer.
            KeyedMutexGuard outputLock;
            if(!outputLock.Acquire(outputMutex_.Get(),0,0,0))return;
            KeyedMutexGuard sourceLock;
            if(!sourceLock.Acquire(sourceMutex_.Get(),0,10,0))return;
            gpu_.context->CopyResource(output_.Get(),source_.Get());
            gpu_.context->End(copyFinished_.Get()); copyPending_=true;
            gpu_.context->Flush();
            sourceLock.Release(0);
            outputLock.Release(1);
            graphicsError_=false;
            memory_.WithLock([&](SharedState& s){
                ++s.frames; s.frameTimeMs=GetTickCount64(); s.status=DriverStatus::Streaming;
            });
        } catch (const std::exception& e) {
            PublishError(e.what());
            outputMutex_.Reset(); output_.Reset(); sourceMutex_.Reset(); source_.Reset(); sourceHandle_=0;
            memory_.WithLock([](SharedState& s){s.frameHandle=0; ++s.frameGeneration;});
        }
    }
    void WaitForPresent() override {
        std::chrono::steady_clock::time_point deadline;
        {
            std::lock_guard<std::mutex> guard(graphicsLock_);
            if (copyPending_ && gpu_.context && copyFinished_) {
                const auto gpuDeadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(100);
                HRESULT hr=S_FALSE;
                while ((hr=gpu_.context->GetData(copyFinished_.Get(),nullptr,0,0))==S_FALSE) {
                    if (std::chrono::steady_clock::now()>=gpuDeadline) {
                        PublishError("FreeLinkVR: GPU copy did not complete within 100 ms."); break;
                    }
                    Sleep(1);
                }
                if (FAILED(hr)) PublishError("FreeLinkVR: GPU completion query failed.");
                copyPending_=false;
            }
            deadline=presentDeadline_;
        }
        std::this_thread::sleep_until(deadline);
    }
    bool GetTimeSinceLastVsync(float* seconds,uint64_t* frame) override {
        const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-epoch_).count();
        *frame=static_cast<uint64_t>(elapsed*hz_);
        *seconds=static_cast<float>(elapsed-static_cast<double>(*frame)/hz_);
        return true;
    }
};

class Provider final : public vr::IServerTrackedDeviceProvider {
    SharedMemory memory_;
    std::unique_ptr<Headset> headset_;
public:
    vr::EVRInitError Init(vr::IVRDriverContext* context) override {
        VR_INIT_SERVER_DRIVER_CONTEXT(context);
        try {
            if (!memory_.Open()) return vr::VRInitError_Driver_Failed;
            SharedState state{};
            if (!memory_.Snapshot(state) || !PoseFresh(state,GetTickCount64())) {
                vr::VRDriverLog()->Log("FreeLinkVR: start FreeLinkVR-Stream.exe before SteamVR.");
                return vr::VRInitError_Init_HmdNotFound;
            }
            headset_=std::make_unique<Headset>(memory_);
            if (!vr::VRServerDriverHost()->TrackedDeviceAdded("SLVR-HMD-001",vr::TrackedDeviceClass_HMD,headset_.get())) {
                headset_.reset(); return vr::VRInitError_Driver_Failed;
            }
            return vr::VRInitError_None;
        } catch (...) {return vr::VRInitError_Driver_Failed;}
    }
    void Cleanup() override {
        if (headset_) headset_->Deactivate();
        headset_.reset(); memory_.Close();
        VR_CLEANUP_SERVER_DRIVER_CONTEXT();
    }
    const char* const* GetInterfaceVersions() override {return vr::k_InterfaceVersions;}
    void RunFrame() override {if (headset_) headset_->RunFrame();}
    bool ShouldBlockStandbyMode() override {return false;}
    void EnterStandby() override {}
    void LeaveStandby() override {}
};
Provider provider;
}

extern "C" __declspec(dllexport) void* HmdDriverFactory(const char* interfaceName,int* error) {
    if (interfaceName && !std::strcmp(interfaceName,vr::IServerTrackedDeviceProvider_Version)) {
        if (error) *error=vr::VRInitError_None;
        return &provider;
    }
    if (error) *error=vr::VRInitError_Init_InterfaceNotFound;
    return nullptr;
}
