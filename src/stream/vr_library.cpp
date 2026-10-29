#include "stream/vr_library.h"
#include "openvr.h"
#include "stream/library_math.h"
#include "stream/library_manifest.h"
#include "common/image_dump.h"
#include "platform/settings.h"
#include "platform/window_focus.h"
#include <fstream>
#include <algorithm>

namespace slvr {
namespace {
constexpr char OverlayKey[]="freelinkvr.library";
constexpr float PanelWidth=1.6f;
void Log(const std::string& line){
    try{const auto path=SettingsPath().parent_path();std::filesystem::create_directories(path);
        std::ofstream(path/L"library.log",std::ios::app)<<GetTickCount64()<<' '<<line<<'\n';}catch(...){}
}
// GDI owns text shaping, font fallback and clipping. Upload one opaque BGRA image.
std::vector<unsigned char> Paint(const std::vector<SteamGame>& games,size_t page,int hover,
                                const std::wstring& status,const std::optional<GazePoint>& gaze){
    HDC dc=CreateCompatibleDC(nullptr);if(!dc)throw std::runtime_error("Cannot create library drawing context");
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=LibraryWidth;
    info.bmiHeader.biHeight=-LibraryHeight;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* bits=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!bitmap){DeleteDC(dc);throw std::runtime_error("Cannot create library image");}
    const auto oldBitmap=SelectObject(dc,bitmap);SetBkMode(dc,TRANSPARENT);
    auto fill=[&](RECT r,COLORREF color){HBRUSH brush=CreateSolidBrush(color);FillRect(dc,&r,brush);DeleteObject(brush);};
    auto text=[&](std::wstring value,RECT rect,int size,COLORREF color,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS){
        HFONT font=CreateFontW(-size,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        const auto old=SelectObject(dc,font);SetTextColor(dc,color);DrawTextW(dc,value.c_str(),static_cast<int>(value.size()),&rect,flags);SelectObject(dc,old);DeleteObject(font);
    };
    const auto white=RGB(240,245,255),muted=RGB(170,188,212);
    fill({0,0,LibraryWidth,LibraryHeight},RGB(16,24,39));
    text(L"FreeLinkVR — Games",{32,24,792,86},40,white);
    auto button=[&](RECT rect,const std::wstring& label,int hit,int size=26){
        fill(rect,hover==hit?RGB(35,109,175):RGB(37,52,73));rect.left+=18;rect.right-=18;text(label,rect,size,white);
    };
    button({800,36,992,90},L"Refresh",-5,23);
    text(L"Look and tap / press A. The list also includes non-VR games.",{32,90,992,130},24,muted);
    for(size_t row=0;row<LibraryRows;++row){
        const size_t index=page*LibraryRows+row;const int top=136+static_cast<int>(row)*78;
        if(index<games.size())button({32,top,992,top+70},games[index].name,static_cast<int>(index),30);
    }
    if(games.empty())text(L"No games found. Add games to Steam and refresh the list.",{32,160,992,235},27,white);
    if(page)button({32,650,262,718},L"← Back",-2);
    text(L"SteamVR tabs are below",{275,650,750,718},24,muted,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    if((page+1)*LibraryRows<games.size())button({762,650,992,718},L"Next →",-3);
    const std::wstring message=status.empty()?L"L / Y — FreeLinkVR    Page "+std::to_wstring(page+1)+L" / "+std::to_wstring(std::max<size_t>(1,(games.size()+LibraryRows-1)/LibraryRows)):status;
    text(message,{32,722,992,762},22,muted);
    if(gaze){const int x=static_cast<int>(gaze->x*LibraryWidth),y=static_cast<int>(gaze->y*LibraryHeight);
        HBRUSH brush=CreateSolidBrush(RGB(255,224,100));const auto old=SelectObject(dc,brush);Ellipse(dc,x-7,y-7,x+7,y+7);SelectObject(dc,old);DeleteObject(brush);}
    std::vector<unsigned char> pixels(static_cast<size_t>(LibraryWidth)*LibraryHeight*4);
    memcpy(pixels.data(),bits,pixels.size());for(size_t i=3;i<pixels.size();i+=4)pixels[i]=255;
    SelectObject(dc,oldBitmap);DeleteObject(bitmap);DeleteDC(dc);return pixels;
}
}
struct VRLibrary::State {
    Graphics& gpu;HMODULE runtime=nullptr;decltype(&vr::VR_ShutdownInternal) shutdown=nullptr;
    vr::IVRSystem* system=nullptr;vr::IVROverlay* overlay=nullptr;vr::IVRApplications* apps=nullptr;vr::IVRCompositor* compositor=nullptr;
    vr::VROverlayHandle_t handle=vr::k_ulOverlayHandleInvalid,thumbnail=vr::k_ulOverlayHandleInvalid;
    ComPtr<ID3D11Texture2D> texture;
    ID3D11ShaderResourceView* monitorFrame=nullptr;
    uint64_t nextMirrorTry=0;
    std::vector<SteamGame> games;size_t page=0;int hover=-1;
    bool initialized=false,visible=false,openRequested=false,dirty=true;
    uint32_t driverPid=0,lastScene=0;uint64_t nextInit=0,nextPaint=0,focusAt=0;
    std::optional<GazePoint> gaze;
    std::wstring status;std::filesystem::path manifest;
    explicit State(Graphics& graphics):gpu(graphics){}
    ~State(){Close();}
    void ReleaseMonitorFrame(){
        if(monitorFrame&&compositor){ID3D11ShaderResourceView* empty=nullptr;gpu.context->PSSetShaderResources(0,1,&empty);compositor->ReleaseMirrorTextureD3D11(monitorFrame);}
        monitorFrame=nullptr;nextMirrorTry=0;
    }
    ID3D11ShaderResourceView* MonitorFrame(){
        if(!initialized||!compositor)return nullptr;
        if(!monitorFrame&&GetTickCount64()>=nextMirrorTry){
            nextMirrorTry=GetTickCount64()+1000;
            const auto result=compositor->GetMirrorTextureD3D11(vr::Eye_Left,gpu.device.Get(),reinterpret_cast<void**>(&monitorFrame));
            if(result!=vr::VRCompositorError_None){monitorFrame=nullptr;Log("monitor mirror unavailable="+std::to_string(result));}
            else{ComPtr<ID3D11Resource> resource;monitorFrame->GetResource(&resource);ComPtr<ID3D11Texture2D> image;if(SUCCEEDED(resource.As(&image))){D3D11_TEXTURE2D_DESC desc{};image->GetDesc(&desc);D3D11_SHADER_RESOURCE_VIEW_DESC view{};monitorFrame->GetDesc(&view);Log("monitor native mirror="+std::to_string(desc.Width)+"x"+std::to_string(desc.Height)+" viewFormat="+std::to_string(view.Format));}}
        }
        return monitorFrame;
    }
    void Close(){
        ReleaseMonitorFrame();
        texture.Reset();
        if(overlay&&handle)overlay->DestroyOverlay(handle);
        if(overlay&&thumbnail)overlay->DestroyOverlay(thumbnail);
        if(apps&&!manifest.empty())apps->RemoveApplicationManifest(Utf8(manifest.wstring()).c_str());
        if(initialized&&shutdown)shutdown();
        if(runtime)FreeLibrary(runtime);
        runtime=nullptr;initialized=false;system=nullptr;overlay=nullptr;apps=nullptr;compositor=nullptr;handle=thumbnail=0;visible=false;hover=-1;gaze.reset();
    }
    void Refresh(){
        auto fresh=SteamGames();std::vector<SteamGame> missing;
        if(!manifest.empty())apps->RemoveApplicationManifest(Utf8(manifest.wstring()).c_str());
        // Never replace metadata registered by Steam or another application.
        for(const auto& game:fresh)if(!apps->IsApplicationInstalled(GameKey(game.id).c_str()))missing.push_back(game);
        if(!missing.empty()){
            manifest=SettingsPath().parent_path()/L"library.vrmanifest";std::filesystem::create_directories(manifest.parent_path());
            std::ofstream file(manifest,std::ios::binary|std::ios::trunc);file<<GameManifest(missing);file.close();
            if(!file)throw std::runtime_error("Cannot save VR library manifest");
            const auto result=apps->AddApplicationManifest(Utf8(manifest.wstring()).c_str(),true);
            if(result!=vr::VRApplicationError_None)throw std::runtime_error(std::string("Library registration: ")+apps->GetApplicationsErrorNameFromEnum(result));
        }
        games=std::move(fresh);page=std::min(page,games.empty()?size_t(0):(games.size()-1)/LibraryRows);hover=-1;dirty=true;status.clear();
        Log("library games="+std::to_string(games.size()));
    }
    void Init(){
        const auto root=FindSteamVR();if(root.empty())throw std::runtime_error("SteamVR not installed");
        runtime=LoadLibraryExW((root/L"bin/win64/openvr_api.dll").c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
        if(!runtime)throw std::runtime_error("Cannot load OpenVR overlay runtime");
        const auto init=reinterpret_cast<decltype(&vr::VR_InitInternal2)>(GetProcAddress(runtime,"VR_InitInternal2"));
        const auto get=reinterpret_cast<decltype(&vr::VR_GetGenericInterface)>(GetProcAddress(runtime,"VR_GetGenericInterface"));
        shutdown=reinterpret_cast<decltype(shutdown)>(GetProcAddress(runtime,"VR_ShutdownInternal"));
        if(!init||!get||!shutdown)throw std::runtime_error("Missing OpenVR exports");
        vr::EVRInitError error=vr::VRInitError_None;init(&error,vr::VRApplication_Overlay,nullptr);
        if(error)throw std::runtime_error("OpenVR overlay init "+std::to_string(error));initialized=true;
        system=static_cast<vr::IVRSystem*>(get(vr::IVRSystem_Version,&error));
        overlay=static_cast<vr::IVROverlay*>(get(vr::IVROverlay_Version,&error));
        apps=static_cast<vr::IVRApplications*>(get(vr::IVRApplications_Version,&error));
        compositor=static_cast<vr::IVRCompositor*>(get(vr::IVRCompositor_Version,&error));
        if(!system||!overlay||!apps||!compositor)throw std::runtime_error("OpenVR interface mismatch");
        const auto result=overlay->CreateDashboardOverlay(OverlayKey,"FreeLinkVR",&handle,&thumbnail);
        if(result!=vr::VROverlayError_None)throw std::runtime_error(std::string("Library overlay: ")+overlay->GetOverlayErrorNameFromEnum(result));
        overlay->SetOverlayWidthInMeters(handle,PanelWidth);
        overlay->SetOverlayFlag(handle,vr::VROverlayFlags_IgnoreTextureAlpha,true);
        overlay->SetOverlayInputMethod(handle,vr::VROverlayInputMethod_Mouse);
        const vr::HmdVector2_t scale{{static_cast<float>(LibraryWidth),static_cast<float>(LibraryHeight)}};
        overlay->SetOverlayMouseScale(handle,&scale);
        std::vector<unsigned char> icon(64*64*4);
        for(int y=0;y<64;++y)for(int x=0;x<64;++x){auto* pixel=icon.data()+(y*64+x)*4;
            const bool mark=(x>=16&&x<24&&y>=12&&y<52)||(x>=24&&x<48&&y>=12&&y<20)||(x>=24&&x<42&&y>=28&&y<36);
            pixel[0]=mark?255:35;pixel[1]=mark?255:109;pixel[2]=mark?255:175;pixel[3]=255;}
        const auto iconResult=overlay->SetOverlayRaw(thumbnail,icon.data(),64,64,4);
        if(iconResult!=vr::VROverlayError_None)throw std::runtime_error("Cannot set dashboard tab icon");
        Refresh();PaintTexture();Log("dashboard tab initialized (not selected)");
    }
    void PaintTexture(){
        auto pixels=Paint(games,page,hover,status,gaze);
        if(!texture){D3D11_TEXTURE2D_DESC desc{};desc.Width=LibraryWidth;desc.Height=LibraryHeight;desc.MipLevels=desc.ArraySize=1;
            desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            Check(gpu.device->CreateTexture2D(&desc,nullptr,&texture),"Create VR library texture");}
        gpu.context->UpdateSubresource(texture.Get(),0,nullptr,pixels.data(),LibraryWidth*4,0);gpu.context->Flush();
        vr::Texture_t image{texture.Get(),vr::TextureType_DirectX,vr::ColorSpace_Gamma};
        const auto result=overlay->SetOverlayTexture(handle,&image);
        if(result!=vr::VROverlayError_None)throw std::runtime_error(std::string("Library texture: ")+overlay->GetOverlayErrorNameFromEnum(result));
        dirty=false;
    }
    void Select(){
        if(!visible||hover==-1)return;
        try{
            if(hover==-2){--page;hover=-1;dirty=true;}
            else if(hover==-3){++page;hover=-1;dirty=true;}
            else if(hover==-5)Refresh();
            else if(hover>=0&&static_cast<size_t>(hover)<games.size()){
                const auto key=GameKey(games[hover].id);
                const auto result=apps->LaunchApplication(key.c_str());
                Log("launch "+key+" result="+std::to_string(result));
                if(result!=vr::VRApplicationError_None)throw std::runtime_error(apps->GetApplicationsErrorNameFromEnum(result));
                focusAt=GetTickCount64()+4000;
            }
        }catch(const std::exception& e){status=Wide(e.what());dirty=true;Log(e.what());}
    }
    void Tick(HWND window,const SharedState& shared,bool allowFocus){
        const auto now=GetTickCount64();
        if(initialized&&driverPid!=shared.driverPid){Close();nextInit=now+2000;}
        if(!initialized){
            if(!shared.driverPid||!shared.frameHandle||now<nextInit)return;
            nextInit=now+5000;driverPid=shared.driverPid;
            try{Init();}catch(const std::exception& e){Log(e.what());Close();return;}
        }
        vr::VREvent_t event{};while(system->PollNextEvent(&event,sizeof(event))){
            if(event.eventType==vr::VREvent_Quit){system->AcknowledgeQuit_Exiting();Close();nextInit=now+5000;return;}
        }
        const auto scene=compositor->GetCurrentSceneFocusProcess();
        if(scene!=lastScene){ReleaseMonitorFrame();Log("scene pid="+std::to_string(scene));lastScene=scene;if(scene)focusAt=now+1500;}
        if(!allowFocus)focusAt=0;
        if(allowFocus&&focusAt&&now>=focusAt){if(FocusStreamingWindow(window))focusAt=0;else focusAt=now+1000;}
        if(openRequested){overlay->ShowDashboard(OverlayKey);openRequested=false;}
        const bool active=overlay->IsActiveDashboardOverlay(handle)&&overlay->IsOverlayVisible(handle);
        if(visible!=active){visible=active;Log(active?"dashboard tab selected":"dashboard tab deselected");}
        if(visible){
            vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
            system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,0,poses,vr::k_unMaxTrackedDeviceCount);
            gaze.reset();
            if(poses[0].bPoseIsValid){
                const auto& head=poses[0].mDeviceToAbsoluteTracking;
                vr::VROverlayIntersectionParams_t ray{};ray.eOrigin=vr::TrackingUniverseStanding;
                for(int i=0;i<3;++i){ray.vSource.v[i]=head.m[i][3];ray.vDirection.v[i]=-head.m[i][2];}
                vr::VROverlayIntersectionResults_t hit{};
                if(overlay->ComputeOverlayIntersection(handle,&ray,&hit))gaze=LibraryGaze(hit.vUVs.v[0],hit.vUVs.v[1]);
            }
            const auto hit=gaze?LibraryHit(gaze->x,gaze->y,games.size(),page):-1;
            if(hit!=hover){hover=hit;dirty=true;}
        }else{gaze.reset();hover=-1;}
        // Native tracked controllers retain normal dashboard pointer interaction.
        while(overlay->PollNextOverlayEvent(handle,&event,sizeof(event))){
            if(!visible)continue;
            if(event.eventType==vr::VREvent_MouseMove||event.eventType==vr::VREvent_MouseButtonDown){
                gaze=LibraryGaze(event.data.mouse.x/LibraryWidth,event.data.mouse.y/LibraryHeight);
                hover=gaze?LibraryHit(gaze->x,gaze->y,games.size(),page):-1;dirty=true;
                if(event.eventType==vr::VREvent_MouseButtonDown&&event.data.mouse.button==vr::VRMouseButton_Left)Select();
            }
        }
        if(dirty||(visible&&now>=nextPaint)){PaintTexture();nextPaint=now+100;}
    }
};
VRLibrary::VRLibrary(Graphics& graphics):state_(std::make_unique<State>(graphics)){}
VRLibrary::~VRLibrary()=default;
void VRLibrary::Tick(HWND window,const SharedState& shared,bool allowFocus){try{state_->Tick(window,shared,allowFocus);}catch(const std::exception& e){Log(e.what());state_->Close();state_->nextInit=GetTickCount64()+5000;}}
bool VRLibrary::Visible()const{return state_->visible;}
uint32_t VRLibrary::SceneProcess()const{return state_->initialized?state_->lastScene:0;}
ID3D11ShaderResourceView* VRLibrary::MonitorFrame(){return state_->MonitorFrame();}
void VRLibrary::Open(){state_->openRequested=true;}
void VRLibrary::Select(){state_->Select();}
void VRLibrary::ResetGraphics(){state_->ReleaseMonitorFrame();if(state_->overlay&&state_->handle)state_->overlay->ClearOverlayTexture(state_->handle);state_->texture.Reset();state_->dirty=true;}
void VRLibrary::Preview(const std::filesystem::path& output){
    const std::vector<SteamGame> games{{1,L"Job Simulator"},{2,L"Half-Life: Alyx"},{3,L"A long game title for testing text clipping and font rendering"}};
    WriteBitmap(output,LibraryWidth,LibraryHeight,Paint(games,0,1,L"",GazePoint{.5f,.34f}));
}
}
