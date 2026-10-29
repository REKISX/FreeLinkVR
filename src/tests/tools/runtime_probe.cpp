// Read-only diagnostics against the installed runtime; never enters the scene slot.
#include "common/graphics.h"
#include "openvr.h"
#include "platform/setup.h"
#include "common/image_dump.h"
#include "common/ipc.h"
#include <iostream>
#include <cmath>
using namespace slvr;
int wmain(int argc,wchar_t** argv){
    HMODULE runtime=nullptr;decltype(&vr::VR_ShutdownInternal) shutdown=nullptr;bool initialized=false;
    try{
        SharedMemory shared;if(!shared.Open())throw std::runtime_error("Cannot read running viewer state");SharedState current{};
        if(!shared.Snapshot(current)||!current.driverPid||!PoseFresh(current,GetTickCount64()))throw std::runtime_error("Start FreeLinkVR and wait for its SteamVR driver before probing; no runtime was started");
        const auto root=FindSteamVR();runtime=LoadLibraryExW((root/L"bin/win64/openvr_api.dll").c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
        if(!runtime)throw std::runtime_error("Cannot load runtime");
        const auto init=reinterpret_cast<decltype(&vr::VR_InitInternal2)>(GetProcAddress(runtime,"VR_InitInternal2"));
        const auto get=reinterpret_cast<decltype(&vr::VR_GetGenericInterface)>(GetProcAddress(runtime,"VR_GetGenericInterface"));
        shutdown=reinterpret_cast<decltype(shutdown)>(GetProcAddress(runtime,"VR_ShutdownInternal"));
        if(!init||!get||!shutdown)throw std::runtime_error("Missing runtime exports");
        vr::EVRInitError error=vr::VRInitError_None;init(&error,vr::VRApplication_Overlay,nullptr);
        if(error)throw std::runtime_error("Init "+std::to_string(error));initialized=true;
        const auto system=static_cast<vr::IVRSystem*>(get(vr::IVRSystem_Version,&error));
        const auto overlay=static_cast<vr::IVROverlay*>(get(vr::IVROverlay_Version,&error));
        const auto apps=static_cast<vr::IVRApplications*>(get(vr::IVRApplications_Version,&error));
        const auto compositor=static_cast<vr::IVRCompositor*>(get(vr::IVRCompositor_Version,&error));
        const auto chaperone=static_cast<vr::IVRChaperone*>(get(vr::IVRChaperone_Version,&error));
        const auto settings=static_cast<vr::IVRSettings*>(get(vr::IVRSettings_Version,&error));
        if(!system||!overlay||!apps||!compositor||!chaperone)throw std::runtime_error("Runtime interfaces");
        vr::VROverlayHandle_t panel=0;const auto found=overlay->FindOverlay("freelinkvr.library",&panel);
        const bool open=argc==2&&std::wstring(argv[1])==L"--open-library";
        if(open){overlay->ShowDashboard("freelinkvr.library");Sleep(500);}
        uint32_t width=0,height=0;if(!found)overlay->GetOverlayImageData(panel,nullptr,0,&width,&height);
        const auto scene=compositor->GetCurrentSceneFocusProcess();char key[vr::k_unMaxApplicationKeyLength]{};apps->GetApplicationKeyByProcessId(scene,key,sizeof(key));
        vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,0,poses,vr::k_unMaxTrackedDeviceCount);
        std::cout<<"overlayFound="<<(found==vr::VROverlayError_None)<<" overlayVisible="<<(!found&&overlay->IsOverlayVisible(panel))
                 <<" overlayTexture="<<width<<'x'<<height<<" dashboardVisible="<<overlay->IsDashboardVisible()
                 <<" libraryTabActive="<<(!found&&overlay->IsActiveDashboardOverlay(panel))
                 <<" scenePid="<<scene<<" sceneKey="<<key<<" hmdValid="<<poses[0].bPoseIsValid<<" calibrationState="<<chaperone->GetCalibrationState()<<'\n';
        vr::Compositor_FrameTiming timing{};timing.m_nSize=sizeof(timing);compositor->GetFrameTiming(&timing);
        std::cout<<"motionSmoothing="<<compositor->IsMotionSmoothingEnabled()<<" motionSupported="<<compositor->IsMotionSmoothingSupported()
                 <<" reprojectionFlags="<<timing.m_nReprojectionFlags<<" dropped="<<timing.m_nNumDroppedFrames
                 <<" frameIndex="<<timing.m_nFrameIndex<<" appGpuMs="<<timing.m_flTotalRenderGpuMs<<" compositorGpuMs="<<timing.m_flCompositorRenderGpuMs<<'\n';
        if(settings){vr::EVRSettingsError settingError=vr::VRSettingsError_None;const bool disabled=settings->GetBool(vr::k_pch_SteamVR_Section,vr::k_pch_SteamVR_DisableAsyncReprojection_Bool,&settingError);std::cout<<"disableAsync="<<disabled<<" settingsError="<<settingError<<'\n';}
        if(argc==2&&!open){
            Graphics gpu;gpu.Open();ID3D11ShaderResourceView* mirror=nullptr;
            const auto result=compositor->GetMirrorTextureD3D11(vr::Eye_Left,gpu.device.Get(),reinterpret_cast<void**>(&mirror));
            if(result!=vr::VRCompositorError_None)throw std::runtime_error("Mirror "+std::to_string(result));
            struct MirrorGuard{vr::IVRCompositor* compositor;void* mirror;~MirrorGuard(){compositor->ReleaseMirrorTextureD3D11(mirror);}} guard{compositor,mirror};
            Sleep(250); // Allow the newly subscribed mirror to receive a frame.
            ComPtr<ID3D11Resource> resource;mirror->GetResource(&resource);ComPtr<ID3D11Texture2D> source;Check(resource.As(&source),"Mirror texture");
            D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            const bool hdr=desc.Format==DXGI_FORMAT_R16G16B16A16_FLOAT;
            std::cout<<"mirrorFormat="<<desc.Format<<" size="<<desc.Width<<'x'<<desc.Height<<'\n';
            if(!hdr&&desc.Format!=DXGI_FORMAT_R8G8B8A8_TYPELESS&&desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM&&desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB&&desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM)throw std::runtime_error("Unsupported mirror pixel format");
            ComPtr<ID3D11Texture2D> staging;Check(gpu.device->CreateTexture2D(&desc,nullptr,&staging),"Mirror staging");gpu.context->CopyResource(staging.Get(),source.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};Check(gpu.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Map mirror");
            std::vector<unsigned char> pixels(static_cast<size_t>(desc.Width)*desc.Height*4);
            auto half=[](uint16_t value){const int exponent=(value>>10)&31;const int mantissa=value&1023;
                const float linear=exponent==31?0.f:std::ldexp(static_cast<float>(exponent?1024+mantissa:mantissa),exponent?exponent-25:-24)*(value&0x8000?-1.f:1.f);
                const float srgb=linear<=.0031308f?12.92f*linear:1.055f*std::pow(linear,1.f/2.4f)-.055f;
                return static_cast<unsigned char>(std::clamp(srgb,0.f,1.f)*255+.5f);};
            for(UINT y=0;y<desc.Height;++y)for(UINT x=0;x<desc.Width;++x){const auto src=static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch+x*(hdr?8:4);auto dst=pixels.data()+(static_cast<size_t>(y)*desc.Width+x)*4;
                if(hdr){const auto channels=reinterpret_cast<const uint16_t*>(src);dst[0]=half(channels[2]);dst[1]=half(channels[1]);dst[2]=half(channels[0]);}
                else{dst[0]=src[desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM?0:2];dst[1]=src[1];dst[2]=src[desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM?2:0];}dst[3]=255;}
            gpu.context->Unmap(staging.Get(),0);WriteBitmap(argv[1],static_cast<int>(desc.Width),static_cast<int>(desc.Height),pixels);
        }
        shutdown();initialized=false;FreeLibrary(runtime);return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';if(initialized&&shutdown)shutdown();if(runtime)FreeLibrary(runtime);return 1;}
}
