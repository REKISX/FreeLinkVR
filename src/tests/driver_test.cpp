#include "openvr_driver.h"
#include "common/graphics.h"
#include "common/ipc.h"
#include "platform/settings.h"
#include "profile.h"
#include <iostream>
#include <map>
#include <vector>
#include <thread>
#include <future>

using namespace slvr;
void Require(bool condition,const char* message) {if (!condition) throw std::runtime_error(message);}

class TestSettings final : public vr::IVRSettings {
public:
    const char* GetSettingsErrorNameFromEnum(vr::EVRSettingsError) override {return "test";}
    void SetBool(const char*,const char*,bool,vr::EVRSettingsError*) override {}
    void SetInt32(const char*,const char*,int32_t,vr::EVRSettingsError*) override {}
    void SetFloat(const char*,const char*,float,vr::EVRSettingsError*) override {}
    void SetString(const char*,const char*,const char*,vr::EVRSettingsError*) override {}
    bool GetBool(const char*,const char*,vr::EVRSettingsError* error) override {if(error)*error=vr::VRSettingsError_None;return true;}
    int32_t GetInt32(const char*,const char* key,vr::EVRSettingsError* error) override {
        if(error)*error=vr::VRSettingsError_None;
        return !std::strcmp(key,"windowWidth") ? 1920 : 1080;
    }
    float GetFloat(const char*,const char* key,vr::EVRSettingsError* error) override {
        const std::map<std::string,float> values={{"refreshRate",60.0f},{"ipdMeters",0.064f},
            {"fovDegrees",90.0f},{"seatedHeightMeters",1.65f},{"distortionK1",0.2f},{"distortionK2",0.1f}};
        const auto found=values.find(key);
        if(error)*error=found==values.end() ? vr::VRSettingsError_UnsetSettingHasNoDefault : vr::VRSettingsError_None;
        return found==values.end() ? 0 : found->second;
    }
    void GetString(const char*,const char*,char* text,uint32_t size,vr::EVRSettingsError*) override {if(size)text[0]=0;}
    void RemoveSection(const char*,vr::EVRSettingsError*) override {}
    void RemoveKeyInSection(const char*,const char*,vr::EVRSettingsError*) override {}
};
class TestProperties final : public vr::IVRProperties {
public:
    std::map<std::pair<uint64_t,int>,std::vector<unsigned char>> values;
    vr::ETrackedPropertyError ReadPropertyBatch(vr::PropertyContainerHandle_t,vr::PropertyRead_t*,uint32_t) override {return vr::TrackedProp_UnknownProperty;}
    vr::ETrackedPropertyError WritePropertyBatch(vr::PropertyContainerHandle_t container,vr::PropertyWrite_t* batch,uint32_t count) override {
        for(uint32_t i=0;i<count;++i) {
            const auto* start=static_cast<unsigned char*>(batch[i].pvBuffer);
            values[{container,batch[i].prop}]=std::vector<unsigned char>(start,start+batch[i].unBufferSize);
            batch[i].eError=vr::TrackedProp_Success;
        }
        return vr::TrackedProp_Success;
    }
    const char* GetPropErrorNameFromEnum(vr::ETrackedPropertyError) override {return "test";}
    vr::PropertyContainerHandle_t TrackedDeviceToPropertyContainer(vr::TrackedDeviceIndex_t index) override {return index+100;}
};
class TestHost final : public vr::IVRServerDriverHost {
public:
    struct Device {vr::ETrackedDeviceClass type;vr::ITrackedDeviceServerDriver* driver;};
    std::vector<Device> devices;
    vr::DriverPose_t pose{};uint32_t poseUpdates=0;
    bool TrackedDeviceAdded(const char*,vr::ETrackedDeviceClass type,vr::ITrackedDeviceServerDriver* driver) override {
        devices.push_back({type,driver});return true;
    }
    void TrackedDevicePoseUpdated(uint32_t,const vr::DriverPose_t& updated,uint32_t) override {pose=updated;++poseUpdates;}
    void VsyncEvent(double) override {}
    void VendorSpecificEvent(uint32_t,vr::EVREventType,const vr::VREvent_Data_t&,double) override {}
    bool IsExiting() override {return false;}
    bool PollNextEvent(vr::VREvent_t*,uint32_t) override {return false;}
    void GetRawTrackedDevicePoses(float,vr::TrackedDevicePose_t*,uint32_t) override {}
    void RequestRestart(const char*,const char*,const char*,const char*) override {}
    uint32_t GetFrameTimings(vr::Compositor_FrameTiming*,uint32_t) override {return 0;}
    void SetDisplayEyeToHead(uint32_t,const vr::HmdMatrix34_t&,const vr::HmdMatrix34_t&) override {}
    void SetDisplayProjectionRaw(uint32_t,const vr::HmdRect2_t&,const vr::HmdRect2_t&) override {}
    void SetRecommendedRenderTargetSize(uint32_t,uint32_t,uint32_t) override {}
};
class TestLog final : public vr::IVRDriverLog {
public:void Log(const char* text) override {std::cout<<"driver: "<<text<<'\n';}
};
class TestManager final : public vr::IVRDriverManager {
public:
    uint32_t GetDriverCount() const override {return 1;}
    uint32_t GetDriverName(vr::DriverId_t,char* text,uint32_t size) override {if(size)text[0]=0;return 1;}
    vr::DriverHandle_t GetDriverHandle(const char*) override {return 1;}
    bool IsEnabled(vr::DriverId_t) const override {return true;}
};
class TestResources final : public vr::IVRResources {
public:
    uint32_t LoadSharedResource(const char*,char*,uint32_t) override {return 0;}
    uint32_t GetResourceFullPath(const char*,const char*,char*,uint32_t) override {return 0;}
};
class TestContext final : public vr::IVRDriverContext {
public:
    TestSettings settings;TestProperties properties;TestHost host;TestLog log;TestManager manager;TestResources resources;
    void* GetGenericInterface(const char* version,vr::EVRInitError* error) override {
        if(error)*error=vr::VRInitError_None;
        if(!std::strcmp(version,vr::IVRSettings_Version))return &settings;
        if(!std::strcmp(version,vr::IVRProperties_Version))return &properties;
        if(!std::strcmp(version,vr::IVRServerDriverHost_Version))return &host;
        if(!std::strcmp(version,vr::IVRDriverLog_Version))return &log;
        if(!std::strcmp(version,vr::IVRDriverManager_Version))return &manager;
        if(!std::strcmp(version,vr::IVRResources_Version))return &resources;
        if(error)*error=vr::VRInitError_Init_InterfaceNotFound;
        return nullptr;
    }
    vr::DriverHandle_t GetDriverHandle() override {return 1;}
};

int wmain(int argc,wchar_t** argv) {
    HMODULE module=nullptr;
    vr::IServerTrackedDeviceProvider* provider=nullptr;
    HANDLE exclusive=nullptr;
    try {
        Require(argc==2,"Pass driver DLL path");
        IsolatedProfile profile(L"driver");UserSettings defaults;defaults.Save();
        // Keep this integration test from altering an active viewer's shared state.
        exclusive=CreateMutexW(nullptr,FALSE,ViewerMutexName);
        Require(exclusive && GetLastError()!=ERROR_ALREADY_EXISTS,"Close FreeLinkVR before running driver integration tests");
        SharedMemory memory;Require(memory.Open(),"Open shared memory for test driver");
        memory.WithLock([](SharedState& s){s.viewerPid=0;s.poseTimeMs=0;},100);
        module=LoadLibraryW(argv[1]);Require(module!=nullptr,"Load actual driver DLL");
        auto factory=reinterpret_cast<void*(*)(const char*,int*)>(GetProcAddress(module,"HmdDriverFactory"));
        Require(factory!=nullptr,"Exported HmdDriverFactory exists");
        int error=0;
        Require(factory("bad interface",&error)==nullptr && error==vr::VRInitError_Init_InterfaceNotFound,"Unknown ABI rejected");
        provider=static_cast<vr::IServerTrackedDeviceProvider*>(factory(vr::IServerTrackedDeviceProvider_Version,&error));
        Require(provider && error==0,"OpenVR provider ABI");
        TestContext context;
        Require(provider->Init(&context)==vr::VRInitError_Init_HmdNotFound,"Driver declines activation without viewer");
        provider->Cleanup();
        memory.WithLock([](SharedState& s){s.viewerPid=GetCurrentProcessId();s.poseTimeMs=GetTickCount64();s.orientation=FromYawPitch(Pi/4,0);},100);
        Require(provider->Init(&context)==vr::VRInitError_None,"Initialize real provider with test OpenVR host");
        Require(context.host.devices.size()==1,"One HMD owns tracking and virtual display");
        auto& hmd=context.host.devices[0];
        Require(hmd.type==vr::TrackedDeviceClass_HMD,"Correct device class");
        Require(hmd.driver->Activate(0)==vr::VRInitError_None,"Activate real HMD");
        const auto capability=context.properties.values.find({100,vr::Prop_HasVirtualDisplayComponent_Bool});
        Require(capability!=context.properties.values.end() && capability->second.size()==sizeof(bool) && capability->second[0],"Active HMD declares virtual-display capability");
        auto display=static_cast<vr::IVRDisplayComponent*>(hmd.driver->GetComponent(vr::IVRDisplayComponent_Version));
        auto video=static_cast<vr::IVRVirtualDisplay*>(hmd.driver->GetComponent(vr::IVRVirtualDisplay_Version));
        Require(display && video,"HMD exposes display and virtual-display components");
        uint32_t x=0,y=0,width=0,height=0;display->GetEyeOutputViewport(vr::Eye_Right,&x,&y,&width,&height);
        Require(x==640 && y==0 && width==640 && height==720,"Right eye occupies right half of configured 720p output");
        auto distorted=display->ComputeDistortion(vr::Eye_Left,0.3f,0.7f);vr::HmdVector2_t inverse{};
        Require(std::abs(distorted.rfGreen[0]-0.3f)<1e-6f&&std::abs(distorted.rfGreen[1]-0.7f)<1e-6f,"Driver does not apply optical correction twice");
        Require(display->ComputeInverseDistortion(&inverse,vr::Eye_Left,0,distorted.rfGreen[0],distorted.rfGreen[1]),"Inverse lens distortion converges");
        Require(std::abs(inverse.v[0]-0.3f)<1e-4f && std::abs(inverse.v[1]-0.7f)<1e-4f,"Lens distortion roundtrip");
        memory.WithLock([](SharedState& s){s.poseTimeMs=GetTickCount64();},100);
        provider->RunFrame();Require(context.host.poseUpdates && context.host.pose.poseIsValid,"Publish live tracked pose");
        Require(std::abs(context.host.pose.qRotation.y-std::sin(Pi/8))<1e-6,"Pose reaches OpenVR without axis change");
        {
            std::promise<void> locked,release;auto released=release.get_future();
            std::thread holder([&]{memory.WithLock([&](SharedState&){locked.set_value();released.wait();},100);});
            locked.get_future().wait();provider->RunFrame();const bool retained=context.host.pose.poseIsValid;
            // The cache must still expire if no new pose can be read.
            Sleep(static_cast<DWORD>(PoseTimeoutMs+20));provider->RunFrame();const bool expired=!context.host.pose.poseIsValid;
            release.set_value();holder.join();
            Require(retained,"IPC contention retains fresh HMD pose");Require(expired,"Retained HMD pose expires during sustained IPC contention");
        }
        memory.WithLock([](SharedState& s){s.poseTimeMs=GetTickCount64()-1000;},100);
        provider->RunFrame();Require(!context.host.pose.poseIsValid,"Stale tracking becomes invalid in actual driver");
        // Feed a real D3D11 shared backbuffer through the actual virtual display ABI.
        Graphics producer,consumer;producer.Open();consumer.Open(&producer.luid);
        D3D11_TEXTURE2D_DESC desc{};desc.Width=32;desc.Height=16;desc.MipLevels=desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
        desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
        std::vector<uint32_t> pixels(32*16,0xff0088dd);D3D11_SUBRESOURCE_DATA data{pixels.data(),32*4,0};
        ComPtr<ID3D11Texture2D> backbuffer;Check(producer.device->CreateTexture2D(&desc,&data,&backbuffer),"Create synthetic compositor frame");
        producer.context->Flush();
        ComPtr<IDXGIResource> resource;Check(backbuffer.As(&resource),"Get synthetic shared resource");
        HANDLE handle=nullptr;Check(resource->GetSharedHandle(&handle),"Get synthetic backbuffer handle");
        vr::PresentInfo_t info{};info.backbufferTextureHandle=reinterpret_cast<uint64_t>(handle);info.nFrameId=1;
        ComPtr<IDXGIKeyedMutex> compositorMutex;Check(backbuffer.As(&compositorMutex),"Query compositor mutex");
        Require(compositorMutex->AcquireSync(0,100)==S_OK,"Hold compositor frame while rendering");
        video->Present(&info,sizeof(info));
        SharedState blocked{};Require(memory.Snapshot(blocked) && blocked.frames==0,"Driver must not read a backbuffer still owned by compositor");
        Check(compositorMutex->ReleaseSync(0),"Publish completed compositor frame");
        video->Present(&info,sizeof(info));video->WaitForPresent();
        SharedState state{};Require(memory.Snapshot(state) && state.frameHandle && state.frames==1,"Actual driver publishes GPU output");
        ComPtr<ID3D11Texture2D> received;Check(consumer.device->OpenSharedResource(reinterpret_cast<HANDLE>(state.frameHandle),IID_PPV_ARGS(&received)),"Open actual driver output");
        ComPtr<IDXGIKeyedMutex> mutex;Check(received.As(&mutex),"Query actual output synchronization");
        Require(mutex->AcquireSync(1,100)==S_OK,"Acquire actual output frame");
        desc.MiscFlags=0;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.Usage=D3D11_USAGE_STAGING;
        ComPtr<ID3D11Texture2D> staging;Check(consumer.device->CreateTexture2D(&desc,nullptr,&staging),"Create driver readback");
        consumer.context->CopyResource(staging.Get(),received.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        Check(consumer.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Read actual driver frame");
        Require(*static_cast<const uint32_t*>(mapped.pData)==0xff0088dd,"Actual driver preserves color pixels");
        consumer.context->Unmap(staging.Get(),0);Check(mutex->ReleaseSync(0),"Release actual output");
        float seconds=0;uint64_t frame=0;Require(video->GetTimeSinceLastVsync(&seconds,&frame) && seconds>=0 && seconds<1.0f/60,"Virtual timing bounded to refresh interval");
        video->Present(nullptr,0);video->Present(&info,0); // Malformed ABI inputs must not crash.
        provider->Cleanup();provider=nullptr;
        Require(memory.Snapshot(state) && !state.driverPid && !state.frameHandle,"Driver cleanup clears shared output");
        memory.WithLock([](SharedState& s){s.viewerPid=0;s.poseTimeMs=0;},100);
        FreeLibrary(module);module=nullptr;CloseHandle(exclusive);exclusive=nullptr;
        std::cout<<"PASS actual DLL ABI, device classes, pose timeout, lens correction, GPU frame and cleanup\n";
        return 0;
    } catch(const std::exception& e) {
        if(provider)provider->Cleanup();
        if(module)FreeLibrary(module);
        if(exclusive)CloseHandle(exclusive);
        std::cerr<<"FAIL "<<e.what()<<'\n';return 1;
    }
}
