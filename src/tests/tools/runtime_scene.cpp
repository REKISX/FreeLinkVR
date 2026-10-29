// Explicit manual test against the installed SteamVR runtime. Not part of CTest.
#include "openvr.h"
#include "common/graphics.h"
#include "common/ipc.h"
#include <d3dcompiler.h>
#include <chrono>
#include <iostream>

using namespace slvr;
const char* SceneShader=R"(
cbuffer Camera : register(b0) { float4 origin; float4 axisX; float4 axisY; float4 axisZ; float4 tangent; };
struct Vertex {float4 position:SV_POSITION;float2 uv:TEXCOORD0;};
Vertex VS(uint id:SV_VertexID) {
 Vertex v;v.uv=float2((id<<1)&2,id&2);v.position=float4(v.uv.x*2-1,1-v.uv.y*2,0,1);return v;
}
float HitSphere(float3 o,float3 d,float3 center,float radius) {
 float3 p=o-center;float b=dot(p,d),c=dot(p,p)-radius*radius,h=b*b-c;
 return h<0 ? 10000 : max(0,-b-sqrt(h));
}
float4 PS(Vertex v):SV_TARGET {
 float sx=lerp(tangent.x,tangent.y,v.uv.x),sy=-lerp(tangent.z,tangent.w,v.uv.y);
 float3 d=normalize(axisX.xyz*sx+axisY.xyz*sy-axisZ.xyz);
 float3 color=lerp(float3(0.1,0.13,0.2),float3(0.32,0.5,0.8),saturate(d.y));
 float ground=d.y<-0.0001 ? -origin.y/d.y : 10000;
 float nearest=10000;
 if(ground>0 && ground<30) {
  float3 p=origin.xyz+d*ground;
  float checker=fmod(abs(floor(p.x)+floor(p.z)),2);
  color=lerp(float3(0.13,0.16,0.18),float3(0.45,0.48,0.5),checker);
  nearest=ground;
 }
 float hit=HitSphere(origin.xyz,d,float3(-0.7,1.5,-3),0.5);
 if(hit>0 && hit<nearest) {
  float3 normal=normalize(origin.xyz+d*hit-float3(-0.7,1.5,-3));
  color=float3(0.8,0.12,0.06)*(0.25+0.75*saturate(dot(normal,normalize(float3(-1,2,1)))));nearest=hit;
 }
 hit=HitSphere(origin.xyz,d,float3(0.7,1.25,-2),0.35);
 if(hit>0 && hit<nearest) {
  float3 normal=normalize(origin.xyz+d*hit-float3(0.7,1.25,-2));
  color=float3(0.08,0.7,0.22)*(0.25+0.75*saturate(dot(normal,normalize(float3(-1,2,1)))));
 }
 return float4(sqrt(saturate(color)),1);
}
)";

int wmain(int argc,wchar_t** argv) {
    HMODULE runtime=nullptr; decltype(&vr::VR_ShutdownInternal) shutdown=nullptr; bool initialized=false;
    try {
        if(argc<2 || argc>3) throw std::runtime_error("Usage: FreeLinkVR-TestScene.exe PATH_TO_OPENVR_API_DLL [seconds]");
        int seconds=argc==3 ? _wtoi(argv[2]) : 30;
        if(seconds<1 || seconds>7200) throw std::runtime_error("Duration must be 1..7200 seconds");
        runtime=LoadLibraryExW(argv[1],nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
        if(!runtime) throw std::runtime_error("Cannot load installed openvr_api.dll");
        auto init=reinterpret_cast<decltype(&vr::VR_InitInternal2)>(GetProcAddress(runtime,"VR_InitInternal2"));
        auto get=reinterpret_cast<decltype(&vr::VR_GetGenericInterface)>(GetProcAddress(runtime,"VR_GetGenericInterface"));
        shutdown=reinterpret_cast<decltype(shutdown)>(GetProcAddress(runtime,"VR_ShutdownInternal"));
        if(!init || !get || !shutdown)throw std::runtime_error("Missing OpenVR runtime exports");
        vr::EVRInitError error=vr::VRInitError_None;
        init(&error,vr::VRApplication_Scene,nullptr);
        if(error)throw std::runtime_error("SteamVR init error "+std::to_string(error));
        initialized=true;
        auto system=static_cast<vr::IVRSystem*>(get(vr::IVRSystem_Version,&error));
        auto compositor=static_cast<vr::IVRCompositor*>(get(vr::IVRCompositor_Version,&error));
        if(!system || !compositor)throw std::runtime_error("SteamVR interface version mismatch "+std::to_string(error));
        for(uint32_t i=0;i<vr::k_unMaxTrackedDeviceCount;++i) {
            const auto type=system->GetTrackedDeviceClass(i);
            if(type==vr::TrackedDeviceClass_Invalid)continue;
            vr::ETrackedPropertyError propertyError=vr::TrackedProp_Success;
            const bool virtualDisplay=system->GetBoolTrackedDeviceProperty(i,vr::Prop_HasVirtualDisplayComponent_Bool,&propertyError);
            std::cout<<"device="<<i<<" class="<<type<<" virtualDisplay="<<virtualDisplay<<" propertyError="<<propertyError<<std::endl;
        }
        SharedMemory memory;if(!memory.Open())throw std::runtime_error("Cannot read FreeLinkVR state");
        SharedState shared{};memory.Snapshot(shared);
        const uint64_t firstFrame=shared.frames;
        Graphics gpu;gpu.Open(shared.driverPid ? &shared.adapterLuid : nullptr);
        uint32_t width=0,height=0;system->GetRecommendedRenderTargetSize(&width,&height);
        std::cout<<"Render target per eye: "<<width<<'x'<<height<<std::endl;
        ComPtr<ID3D11Texture2D> eyes[2];ComPtr<ID3D11RenderTargetView> targets[2];
        D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.ArraySize=desc.MipLevels=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
        desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        for(int i=0;i<2;++i){Check(gpu.device->CreateTexture2D(&desc,nullptr,&eyes[i]),"Create eye texture");Check(gpu.device->CreateRenderTargetView(eyes[i].Get(),nullptr,&targets[i]),"Create eye target");}
        D3D11_TEXTURE2D_DESC readbackDesc=desc;
        readbackDesc.Usage=D3D11_USAGE_STAGING;readbackDesc.BindFlags=0;
        readbackDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> readback;
        Check(gpu.device->CreateTexture2D(&readbackDesc,nullptr,&readback),"Create scene readback");
        auto compile=[&](const char* entry,const char* profile){
            ComPtr<ID3DBlob> code,errors;
            auto hr=D3DCompile(SceneShader,std::strlen(SceneShader),"scene",nullptr,nullptr,entry,profile,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
            if(FAILED(hr) && errors)throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
            Check(hr,"Compile scene");return code;
        };
        auto vs=compile("VS","vs_5_0"),ps=compile("PS","ps_5_0");
        ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel;
        Check(gpu.device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex),"Scene vertex shader");
        Check(gpu.device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel),"Scene pixel shader");
        struct Camera {float origin[4],axisX[4],axisY[4],axisZ[4],tangent[4];};
        D3D11_BUFFER_DESC bufferDesc{};bufferDesc.ByteWidth=sizeof(Camera);bufferDesc.Usage=D3D11_USAGE_DEFAULT;bufferDesc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        ComPtr<ID3D11Buffer> camera;Check(gpu.device->CreateBuffer(&bufferDesc,nullptr,&camera),"Scene camera buffer");
        compositor->SetTrackingSpace(vr::TrackingUniverseStanding);
        const auto start=std::chrono::steady_clock::now();uint64_t submitted=0;
        int lastReport=-1;uint64_t validPoses=0,poseChanges=0;
        vr::HmdMatrix34_t previousPose{};bool havePreviousPose=false;
        vr::EVRCompositorError lastError=vr::VRCompositorError_None;
        while(std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<seconds) {
            vr::VREvent_t event{};bool quit=false;
            while(system->PollNextEvent(&event,sizeof(event)))if(event.eventType==vr::VREvent_Quit)quit=true;
            if(quit)break;
            vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
            auto wait=compositor->WaitGetPoses(poses,vr::k_unMaxTrackedDeviceCount,nullptr,0);
            if(wait!=vr::VRCompositorError_None)throw std::runtime_error("WaitGetPoses error "+std::to_string(wait));
            auto pose=poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking;
            if(poses[0].bPoseIsValid)++validPoses;
            if(!poses[0].bPoseIsValid)pose={{{1,0,0,0},{0,1,0,1.65f},{0,0,1,0}}};
            if(poses[0].bPoseIsValid) {
                bool different=false;
                for(int row=0;row<3;++row)for(int column=0;column<3;++column)
                    different=different || std::abs(pose.m[row][column]-previousPose.m[row][column])>1e-5f;
                if(havePreviousPose && different)++poseChanges;
                previousPose=pose;havePreviousPose=true;
            }
            for(int i=0;i<2;++i) {
                const auto eye=static_cast<vr::EVREye>(i);auto local=system->GetEyeToHeadTransform(eye);Camera c{};
                for(int row=0;row<3;++row) {
                    c.origin[row]=pose.m[row][3];
                    for(int column=0;column<3;++column)c.origin[row]+=pose.m[row][column]*local.m[column][3];
                    c.axisX[row]=pose.m[row][0];c.axisY[row]=pose.m[row][1];c.axisZ[row]=pose.m[row][2];
                }
                system->GetProjectionRaw(eye,&c.tangent[0],&c.tangent[1],&c.tangent[2],&c.tangent[3]);
                gpu.context->UpdateSubresource(camera.Get(),0,nullptr,&c,0,0);
                D3D11_VIEWPORT viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1};gpu.context->RSSetViewports(1,&viewport);
                auto target=targets[i].Get();gpu.context->OMSetRenderTargets(1,&target,nullptr);
                gpu.context->IASetInputLayout(nullptr);gpu.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                gpu.context->VSSetShader(vertex.Get(),nullptr,0);gpu.context->PSSetShader(pixel.Get(),nullptr,0);
                auto buffer=camera.Get();gpu.context->PSSetConstantBuffers(0,1,&buffer);gpu.context->Draw(3,0);
                gpu.context->OMSetRenderTargets(0,nullptr,nullptr);gpu.context->Flush();
                vr::Texture_t texture{eyes[i].Get(),vr::TextureType_DirectX,vr::ColorSpace_Gamma};
                auto result=compositor->Submit(eye,&texture);
                if(result!=vr::VRCompositorError_None && result!=lastError){std::cout<<"Submit error "<<result<<std::endl;lastError=result;}
                if(result==vr::VRCompositorError_None)++submitted;
            }
            compositor->PostPresentHandoff();
            const int elapsed=static_cast<int>(std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count());
            if(elapsed!=lastReport && elapsed%5==0) {
                memory.Snapshot(shared);
                gpu.context->CopyResource(readback.Get(),eyes[0].Get());
                D3D11_MAPPED_SUBRESOURCE pixels{};
                Check(gpu.context->Map(readback.Get(),0,D3D11_MAP_READ,0,&pixels),"Read scene pixels");
                uint64_t imageHash=14695981039346656037ULL;
                for(uint32_t y=0;y<height;y+=16)for(uint32_t x=0;x<width*4;x+=4*16) {
                    const auto bytes=static_cast<const uint8_t*>(pixels.pData)+y*pixels.RowPitch+x;
                    for(int channel=0;channel<3;++channel){imageHash^=bytes[channel];imageHash*=1099511628211ULL;}
                }
                gpu.context->Unmap(readback.Get(),0);
                std::cout<<"seconds="<<elapsed<<" eyeSubmits="<<submitted<<" driverFrames="<<shared.frames
                    <<" hmdPoseValid="<<poses[0].bPoseIsValid<<" poseChanges="<<poseChanges
                    <<" forward="<<-pose.m[0][2]<<','<<-pose.m[1][2]<<','<<-pose.m[2][2]
                    <<" ipcQ="<<shared.orientation.w<<','<<shared.orientation.x<<','<<shared.orientation.y<<','<<shared.orientation.z
                    <<" imageHash="<<imageHash<<" scenePid="<<compositor->GetCurrentSceneFocusProcess()
                    <<" selfPid="<<GetCurrentProcessId()<<" canRender="<<compositor->CanRenderScene()<<std::endl;
                lastReport=elapsed;
            }
        }
        const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        memory.Snapshot(shared);
        const uint64_t delivered=shared.frames>=firstFrame ? shared.frames-firstFrame : 0;
        std::cout<<"RESULT eyeSubmits="<<submitted<<" driverFramesDelta="<<delivered<<" validPoses="<<validPoses<<" poseChanges="<<poseChanges<<" seconds="<<elapsed<<std::endl;
        shutdown();initialized=false;FreeLibrary(runtime);runtime=nullptr;
        return submitted && delivered && validPoses && elapsed>=seconds ? 0 : 2;
    }catch(const std::exception& e){
        std::cerr<<"FAIL "<<e.what()<<std::endl;
        if(initialized && shutdown)shutdown();if(runtime)FreeLibrary(runtime);return 1;
    }
}
