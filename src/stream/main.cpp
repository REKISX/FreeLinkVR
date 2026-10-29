#include "stream/udp.h" // Winsock must precede windows.h.
#include "common/graphics.h"
#include "common/ipc.h"
#include "stream/input_diagnostics.h"
#include "platform/input_context.h"
#include "platform/settings.h"
#include "platform/setup.h"
#include "stream/runtime_startup.h"
#include "stream/frame_receiver.h"
#include "common/image_dump.h"
#include "stream/vr_library.h"
#include "stream/monitor_view.h"
#include "platform/window_focus.h"
#include <d3dcompiler.h>
#include <shellapi.h>
#include <chrono>
#include <memory>
#include <string>

namespace {
using namespace slvr;
struct Options {
    std::string address,token;
    uint16_t port=4242;
    bool selfTest=false;
    bool capture=false,fullscreen=false;
    bool inputDiagnostics=false;
    int diagnosticSeconds=0;
    std::filesystem::path steamApi;
};
std::string Narrow(const wchar_t* value) {
    std::wstring s(value);
    if (s.find_first_not_of(L"0123456789abcdefABCDEF.: ")!=std::wstring::npos)
        throw std::runtime_error("Invalid command-line argument");
    std::string result; result.reserve(s.size());
    for (wchar_t character:s) result.push_back(static_cast<char>(character));
    return result;
}
Options ParseOptions() {
    int argc=0;
    wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if (!argv) throw std::runtime_error("Cannot parse command line");
    Options options;
    try {
        for (int i=1;i<argc;++i) {
            const std::wstring key=argv[i];
            if (key==L"--self-test") {options.selfTest=true; continue;}
            if (key==L"--capture") {options.capture=true;continue;}
            if (key==L"--fullscreen") {options.fullscreen=true;continue;}
            if (key==L"--input-diagnostics") {options.inputDiagnostics=true;continue;}
            if (key==L"--steam-api" && i+1<argc) {options.steamApi=argv[++i];continue;}
            if (key==L"--diagnostic-seconds" && i+1<argc) {
                wchar_t* end=nullptr;const wchar_t* value=argv[++i];long seconds=wcstol(value,&end,10);
                if(!*value || *end || seconds<1 || seconds>7200)throw std::runtime_error("Diagnostic duration must be 1..7200 seconds");
                options.diagnosticSeconds=static_cast<int>(seconds);continue;
            }
            if ((key!=L"--listen" && key!=L"--port" && key!=L"--token") || i+1>=argc)
                throw std::runtime_error("Usage: FreeLinkVR-Stream.exe [--listen IPv4 --port 4242 --token 32_HEX_DIGITS]");
            const wchar_t* value=argv[++i];
            if (key==L"--listen") options.address=Narrow(value);
            else if (key==L"--token") options.token=Narrow(value);
            else {
                wchar_t* end=nullptr;
                unsigned long port=wcstoul(value,&end,10);
                if (!*value || *end || port==0 || port>65535) throw std::runtime_error("Invalid UDP port");
                options.port=static_cast<uint16_t>(port);
            }
        }
        if (options.address.empty() && !options.token.empty()) throw std::runtime_error("--token requires --listen");
        if(options.inputDiagnostics && !options.address.empty())throw std::runtime_error("UDP and input diagnostics are separate modes");
        if(!options.inputDiagnostics && (options.diagnosticSeconds || !options.steamApi.empty()))throw std::runtime_error("Diagnostic options require --input-diagnostics");
        LocalFree(argv); return options;
    } catch (...) {LocalFree(argv); throw;}
}

const char* Shader=R"(
Texture2D image : register(t0);
SamplerState linearSampler : register(s0);
struct Vertex { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Vertex VS(uint id : SV_VertexID) {
    Vertex v;
    v.uv=float2((id<<1)&2,id&2);
    v.position=float4(v.uv.x*2-1,1-v.uv.y*2,0,1);
    return v;
}
cbuffer Optics : register(b0) {float4 lens;float4 outputParams;};
float2 LensUV(float2 uv,out bool valid) {
    float eyeIndex=step(0.5,uv.x);
    float2 local=float2(uv.x*2-eyeIndex,uv.y);
    float2 p=(local-0.5)*2;
    float r2=dot(p,p);
    if(lens.w>0.5)local=0.5+p*(1+lens.x*r2+lens.y*r2*r2)*0.5;
    valid=all(local>=0)&&all(local<=1);
    return float2((local.x+eyeIndex)*0.5,local.y);
}
float4 PS(Vertex v) : SV_TARGET {
    bool valid;float2 uv=LensUV(v.uv,valid);if(!valid)return float4(0,0,0,1);
    uint width,height;image.GetDimensions(width,height);
    float eyeIndex=step(0.5,v.uv.x);
    uv.x=clamp(uv.x,eyeIndex*0.5+0.5/width,(eyeIndex+1)*0.5-0.5/width);
    uv.y=clamp(uv.y,0.5/height,1-0.5/height);
    return float4(image.Sample(linearSampler,uv).rgb,1);
}
float4 Grid(Vertex v) : SV_TARGET {
    bool valid;float2 uv=LensUV(v.uv,valid);if(!valid)return float4(0,0,0,1);
    float2 eye=float2(frac(uv.x*2),uv.y);
    float2 gridDistance=abs(frac(eye*10)-0.5);
    float grid=1-step(0.035,min(gridDistance.x,gridDistance.y));
    float cross=1-step(0.004,min(abs(eye.x-0.5),abs(eye.y-0.5)));
    float3 base=v.uv.x<0.5 ? float3(0.08,0.18,0.32) : float3(0.25,0.09,0.12);
    return float4(base+grid*0.25+cross*0.5,1);
}
// Local monitor output uses raw left-eye pixels, without headset lens warp.
float2 MonoUV(float2 uv){return float2(lens.w+(uv.x-.5)*lens.x*lens.z,.5+(uv.y-.5)*lens.y);}
float3 EncodeSRGB(float3 value){
    value=max(value,0);
    return lerp(value*12.92,1.055*pow(value,1.0/2.4)-.055,step(.0031308,value));
}
float4 Mono(Vertex v) : SV_TARGET {
    float2 uv=MonoUV(v.uv);uint width,height;image.GetDimensions(width,height);
    float minU=lens.w-lens.z*.5+.5/width,maxU=lens.w+lens.z*.5-.5/width;
    uv.x=clamp(uv.x,minU,maxU);uv.y=clamp(uv.y,.5/height,1-.5/height);
    float3 c=image.Sample(linearSampler,uv).rgb;
    float2 texel=1.0/float2(width,height);
    float3 l=image.Sample(linearSampler,float2(max(minU,uv.x-texel.x),uv.y)).rgb;
    float3 r=image.Sample(linearSampler,float2(min(maxU,uv.x+texel.x),uv.y)).rgb;
    float3 t=image.Sample(linearSampler,float2(uv.x,max(.5/height,uv.y-texel.y))).rgb;
    float3 b=image.Sample(linearSampler,float2(uv.x,min(1-.5/height,uv.y+texel.y))).rgb;
    float3 color=saturate(c+(c-(l+r+t+b)*.25)*.18);
    if(outputParams.x>.5)color=EncodeSRGB(color);
    return float4(color,1);
}
float4 MonoGrid(Vertex v) : SV_TARGET {
    float2 eye=float2(MonoUV(v.uv).x*2,MonoUV(v.uv).y);
    float2 distance=abs(frac(eye*10)-.5);float grid=1-step(.035,min(distance.x,distance.y));
    float cross=1-step(.004,min(abs(eye.x-.5),abs(eye.y-.5)));
    return float4(float3(.08,.18,.32)+grid*.25+cross*.5,1);
}
)";

class Viewer {
    HWND window_=nullptr;
    SharedMemory memory_;
    Graphics gpu_;
    ComPtr<IDXGISwapChain> swapchain_;
    ComPtr<ID3D11RenderTargetView> target_;
    ComPtr<ID3D11VertexShader> vertexShader_;
    ComPtr<ID3D11PixelShader> pixelShader_,gridShader_;
    ComPtr<ID3D11PixelShader> monoShader_,monoGridShader_;
    ComPtr<ID3D11SamplerState> sampler_;
    FrameReceiver frames_{gpu_};
    ComPtr<ID3D11Buffer> optics_;
    ComPtr<ID3D11Buffer> monoOptions_;
    std::unique_ptr<MonitorView> monitor_;
    bool monitorEnabled_=true,protected_=false;
    uint64_t monitorRetryAt_=0,nextMonitorFrameAt_=0;
    std::string monitorError_;
    std::filesystem::path runtimeRoot_;
    uint64_t nextFocusCheck_=0;
    UserSettings settings_;
    std::unique_ptr<RuntimeStartup> startup_;
    std::unique_ptr<VRLibrary> library_;
    bool selfTest_=false;
    int testFrames_=0;
    uint64_t lastTitleMs_=0,udpTimeMs_=0;
    UdpReceiver udp_;
    bool udpMode_=false,captured_=false,captureWanted_=false,fullscreen_=false;
    bool trackingEnabled_=true;
    double yaw_=0,pitch_=0;
    MouseMotion mouseMotion_;
    POINT freeMousePosition_{};
    bool freeMousePositionValid_=false;
    Quaternion raw_{},center_{};
    RECT savedRect_{};
    DWORD savedStyle_=0;
    std::string lastTextureError_;
    bool diagnosticsRequested_=false;
    bool wasMinimized_=false,captureResetPending_=false;
    bool desktopParked_=false;
    HRESULT lastPresent_=E_PENDING;
    void LogWindowEvent(const char* event) const {
        if(selfTest_)return;
        try{std::ofstream(SettingsPath().parent_path()/L"session.log",std::ios::app)
            <<GetTickCount64()<<" viewer="<<GetCurrentProcessId()<<" "<<event<<'\n';}catch(...){}
    }
public:
    bool DiagnosticsRequested() const {return diagnosticsRequested_;}
    int FramesPerSecond() const{return settings_.fps;}
    ~Viewer() {
        monitor_.reset();
        Capture(false);
        memory_.WithLock([](SharedState& s){s.viewerPid=0; s.poseTimeMs=0;},100);
        if (window_) DestroyWindow(window_);
    }
    static LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM w,LPARAM l) {
        Viewer* viewer=reinterpret_cast<Viewer*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if (message==WM_NCCREATE) {
            viewer=static_cast<Viewer*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            viewer->window_=window;
            SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(viewer));
        }
        return viewer ? viewer->HandleMessage(message,w,l) : DefWindowProcW(window,message,w,l);
    }
    void Capture(bool capture,bool remember=true) {
        if(remember) {captureWanted_=capture; trackingEnabled_=capture;}
        freeMousePositionValid_=false;
        if (capture==captured_) return;
        captured_=capture;
        mouseMotion_={};
        if (capture) {
            SetCapture(window_);
            ShowCursor(FALSE);
            CenterCursor();
        } else {
            ClipCursor(nullptr);
            ReleaseCapture();
            ShowCursor(TRUE);
        }
    }
    void CenterCursor() {
        RECT rect{}; GetClientRect(window_,&rect);
        POINT tl{rect.left,rect.top},br{rect.right,rect.bottom};
        ClientToScreen(window_,&tl); ClientToScreen(window_,&br);
        RECT clip{tl.x,tl.y,br.x,br.y}; ClipCursor(&clip);
        SetCursorPos((tl.x+br.x)/2,(tl.y+br.y)/2);
    }
    void ToggleFullscreen() {
        const bool restoreCapture=captured_;
        Capture(false,false);
        fullscreen_=!fullscreen_;
        if (fullscreen_) {
            savedStyle_=static_cast<DWORD>(GetWindowLongPtrW(window_,GWL_STYLE));
            GetWindowRect(window_,&savedRect_);
            MONITORINFO monitor{sizeof(monitor)};
            GetMonitorInfoW(MonitorFromWindow(window_,MONITOR_DEFAULTTONEAREST),&monitor);
            SetWindowLongPtrW(window_,GWL_STYLE,savedStyle_&~WS_OVERLAPPEDWINDOW);
            SetWindowPos(window_,HWND_TOP,monitor.rcMonitor.left,monitor.rcMonitor.top,
                monitor.rcMonitor.right-monitor.rcMonitor.left,monitor.rcMonitor.bottom-monitor.rcMonitor.top,SWP_FRAMECHANGED);
        } else {
            SetWindowLongPtrW(window_,GWL_STYLE,savedStyle_);
            SetWindowPos(window_,nullptr,savedRect_.left,savedRect_.top,
                savedRect_.right-savedRect_.left,savedRect_.bottom-savedRect_.top,SWP_NOZORDER|SWP_FRAMECHANGED);
        }
        Capture(restoreCapture,false);
    }
    void ParkOnDesktop(){
        if(desktopParked_)return;
        desktopParked_=true;Capture(false,false);
        if(monitor_)monitor_->HideLocal();
        protected_=false;
        SetWindowPos(window_,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        SetWindowPos(window_,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        // An iconic capture window makes Steam abandon the game's surface.
        // Keep it drawable behind other apps and release foreground ownership.
        if(!selfTest_)EnumWindows([](HWND candidate,LPARAM)->BOOL{
            DWORD pid=0;GetWindowThreadProcessId(candidate,&pid);
            if(pid==GetCurrentProcessId()||!IsWindowVisible(candidate)||IsIconic(candidate)||
                GetWindow(candidate,GW_OWNER)||(GetWindowLongPtrW(candidate,GWL_EXSTYLE)&(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE)))return TRUE;
            return SetForegroundWindow(candidate)?FALSE:TRUE;
        },0);
        LogWindowEvent("local preview parked; capture window remains drawable");
    }
    void RestoreLocalPreview(){
        if(!desktopParked_)return;
        desktopParked_=false;LogWindowEvent("local preview restored; capture surface preserved");
    }
    LRESULT HandleMessage(UINT message,WPARAM w,LPARAM l) {
        switch(message) {
        case WM_SYSCOMMAND:
            if((w&0xfff0)==SC_MINIMIZE){ParkOnDesktop();return 0;}
            if((w&0xfff0)==SC_RESTORE&&desktopParked_){RestoreLocalPreview();if(!selfTest_)FocusStreamingWindow(window_);return 0;}
            break;
        case WM_ACTIVATE:
            if(LOWORD(w)!=WA_INACTIVE)RestoreLocalPreview();
            break;
        case slvr::RequestInputDiagnostics:
            diagnosticsRequested_=true;PostMessageW(window_,WM_CLOSE,0,0);return 0;
        case WM_CLOSE: LogWindowEvent("close requested");DestroyWindow(window_); return 0;
        case WM_SIZE:
            if(w==SIZE_MINIMIZED){wasMinimized_=true;LogWindowEvent("window minimized");}
            else if(wasMinimized_&&(w==SIZE_RESTORED||w==SIZE_MAXIMIZED)){
                wasMinimized_=false;captureResetPending_=true;
                LogWindowEvent("window restored; capture reset scheduled");
            }
            return 0;
        case WM_DESTROY: if(monitor_)monitor_->Hide();Capture(false); window_=nullptr; PostQuitMessage(0); return 0;
        case WM_KILLFOCUS: mouseMotion_={}; Capture(false,false); return 0;
        case WM_SETFOCUS: if(captureWanted_ && !udpMode_)Capture(true,false); return 0;
        case WM_CAPTURECHANGED:
            if (captured_ && reinterpret_cast<HWND>(l)!=window_) Capture(false,false);
            return 0;
        case WM_LBUTTONDOWN:
            if (!udpMode_) trackingEnabled_=true;
            if(library_&&library_->Visible())library_->Select();return 0;
        case WM_KEYDOWN:
            if (l&(1LL<<30)) return 0;
            if (w==VK_ESCAPE) {Capture(false);return 0;}
            if (w==VK_F8) {monitorEnabled_=!monitorEnabled_;if(!monitorEnabled_&&monitor_)monitor_->Hide();return 0;}
            if(w==VK_OEM_MINUS||w==VK_SUBTRACT){settings_.monitorScale=std::max(.5f,settings_.monitorScale-.05f);return 0;}
            if(w==VK_OEM_PLUS||w==VK_ADD){settings_.monitorScale=std::min(1.f,settings_.monitorScale+.05f);return 0;}
            if (w=='L'&&library_) {trackingEnabled_=true;library_->Open();return 0;}
            if ((w==VK_RETURN||w==VK_SPACE)&&library_) {trackingEnabled_=true;if(library_->Visible())library_->Select();return 0;}
            if (w==VK_F11) {ToggleFullscreen(); return 0;}
            if (w==VK_F2) {diagnosticsRequested_=true;PostMessageW(window_,WM_CLOSE,0,0);return 0;}
            if (w=='M' && !udpMode_) {Capture(!captured_); return 0;}
            if (w=='R') {center_=Inverse(raw_); yaw_=pitch_=0; if (!udpMode_) {raw_={}; center_={};} return 0;}
            break;
        case WM_INPUT:
            // INPUTSINK keeps gyro-to-mouse alive when SteamVR takes focus.
            // Explicit pause, not foreground-window ownership, gates tracking.
            if(trackingEnabled_ && !udpMode_) {
                RAWINPUT input{};UINT size=sizeof(input);
                if(GetRawInputData(reinterpret_cast<HRAWINPUT>(l),RID_INPUT,&input,&size,sizeof(RAWINPUTHEADER))!=static_cast<UINT>(-1) &&
                    input.header.dwType==RIM_TYPEMOUSE && !(input.data.mouse.usFlags&MOUSE_MOVE_ABSOLUTE))
                    mouseMotion_.AddRaw(input.data.mouse.lLastX,input.data.mouse.lLastY);
            }
            break; // DefWindowProc performs WM_INPUT cleanup.
        case WM_MOUSEMOVE:
            if (trackingEnabled_ && !udpMode_ && GetForegroundWindow()==window_) {
                const POINT position{static_cast<short>(LOWORD(l)),static_cast<short>(HIWORD(l))};
                if(captured_) {
                    RECT rect{}; GetClientRect(window_,&rect);
                    mouseMotion_.SetLegacy(position.x-rect.right/2,position.y-rect.bottom/2);
                } else {
                    if(freeMousePositionValid_)
                        mouseMotion_.AddLegacyDelta(position.x-freeMousePosition_.x,position.y-freeMousePosition_.y);
                    freeMousePosition_=position;freeMousePositionValid_=true;
                }
            }
            return 0;
        }
        return DefWindowProcW(window_,message,w,l);
    }
    void Create(HWND window,const Options& options) {
        window_=window;
        selfTest_=options.selfTest;settings_=options.selfTest?UserSettings{}:UserSettings::Load();monitorEnabled_=settings_.monitorPreview;
        if(!options.selfTest&&options.address.empty())startup_=std::make_unique<RuntimeStartup>();
        if(!options.selfTest)runtimeRoot_=FindSteamVR();
        if (!memory_.Open()) throw std::runtime_error("Cannot open driver shared memory");
        udpMode_=!options.address.empty();
        RAWINPUTDEVICE mouse{0x01,0x02,RIDEV_INPUTSINK,window_};
        if(!RegisterRawInputDevices(&mouse,1,sizeof(mouse)))throw std::runtime_error("Cannot register relative mouse input");
        if (udpMode_) udp_.Open(options.address,options.port,options.token);
        SharedState state{}; memory_.Snapshot(state);
        const LUID* desired=state.driverPid && state.frameHandle ? &state.adapterLuid : nullptr;
        gpu_.Open(desired);
        CreateGraphics();
        if(!selfTest_)library_=std::make_unique<VRLibrary>(gpu_);
        else {TestMonitorColor();VRLibrary::Preview(std::filesystem::current_path()/L"library-preview.bmp");}
    }
    void CreateGraphics() {
        monitor_.reset();monoShader_.Reset();monoGridShader_.Reset();monoOptions_.Reset();
        frames_.Reset(); optics_.Reset();
        target_.Reset(); swapchain_.Reset();
        vertexShader_.Reset(); pixelShader_.Reset(); gridShader_.Reset(); sampler_.Reset();
        CreateCaptureSwapchain();
        auto compile=[&](const char* entry,const char* profile) {
            ComPtr<ID3DBlob> code,errors;
            HRESULT hr=D3DCompile(Shader,std::strlen(Shader),"viewer",nullptr,nullptr,entry,profile,
                D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
            if (FAILED(hr) && errors) throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
            Check(hr,"Compile viewer shader"); return code;
        };
        auto vs=compile("VS","vs_5_0"),ps=compile("PS","ps_5_0"),grid=compile("Grid","ps_5_0");
        Check(gpu_.device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertexShader_),"Create vertex shader");
        Check(gpu_.device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixelShader_),"Create pixel shader");
        Check(gpu_.device->CreatePixelShader(grid->GetBufferPointer(),grid->GetBufferSize(),nullptr,&gridShader_),"Create grid shader");
        auto mono=compile("Mono","ps_5_0"),monoGrid=compile("MonoGrid","ps_5_0");
        Check(gpu_.device->CreatePixelShader(mono->GetBufferPointer(),mono->GetBufferSize(),nullptr,&monoShader_),"Create monitor shader");
        Check(gpu_.device->CreatePixelShader(monoGrid->GetBufferPointer(),monoGrid->GetBufferSize(),nullptr,&monoGridShader_),"Create monitor grid shader");
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD=D3D11_FLOAT32_MAX;
        Check(gpu_.device->CreateSamplerState(&sampler,&sampler_),"Create sampler");
        const float lens[8]={settings_.lensStrength,settings_.lensStrength*0.2f,1.0f,settings_.lenses?1.0f:0.0f,0,0,0,0};
        D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=sizeof(lens);buffer.Usage=D3D11_USAGE_DEFAULT;buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA data{lens,0,0};Check(gpu_.device->CreateBuffer(&buffer,&data,&optics_),"Create optics buffer");
        Check(gpu_.device->CreateBuffer(&buffer,nullptr,&monoOptions_),"Create monitor options buffer");
    }
    void CreateCaptureSwapchain(){
        gpu_.context->OMSetRenderTargets(0,nullptr,nullptr);target_.Reset();swapchain_.Reset();gpu_.context->Flush();
        ComPtr<IDXGIDevice> dxgi; Check(gpu_.device.As(&dxgi),"Query DXGI device");
        ComPtr<IDXGIAdapter> adapter; Check(dxgi->GetAdapter(&adapter),"Get adapter");
        ComPtr<IDXGIFactory> factory; Check(adapter->GetParent(IID_PPV_ARGS(&factory)),"Get factory");
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count=1; desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount=2; desc.OutputWindow=window_; desc.Windowed=TRUE;
        desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
        Check(factory->CreateSwapChain(gpu_.device.Get(),&desc,&swapchain_),"Create window swap chain");
        Check(factory->MakeWindowAssociation(window_,DXGI_MWA_NO_ALT_ENTER),"Set window association");
    }
    void ReceiveFrame(const SharedState& state) {
        if(state.driverPid&&state.frameHandle&&!SameLuid(state.adapterLuid,gpu_.luid)) {
            monitor_.reset();
            if(library_)library_->ResetGraphics();
            gpu_.context->ClearState();gpu_.context->Flush();frames_.Reset();
            target_.Reset();swapchain_.Reset();vertexShader_.Reset();pixelShader_.Reset();gridShader_.Reset();sampler_.Reset();optics_.Reset();
            gpu_.Open(&state.adapterLuid);CreateGraphics();
        }
        frames_.Receive(state);
        if(frames_.ReceivedAt()&&GetTickCount64()-frames_.ReceivedAt()<1000)lastTextureError_.clear();
    }
    void Tick() {
        const uint64_t now=GetTickCount64();
        if(!selfTest_){
            const bool protect=trackingEnabled_&&!desktopParked_&&!IsIconic(window_);
            if(protected_!=protect){protected_=protect;SetWindowPos(window_,protected_?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);}
            if(!trackingEnabled_&&monitor_)monitor_->Hide();
        }
        if(trackingEnabled_ && !udpMode_) {
            const auto delta=mouseMotion_.Take();
            if(delta.first || delta.second) {
                const double radiansPerPixel=0.002*settings_.sensitivity;
                yaw_=std::remainder(yaw_-delta.first*radiansPerPixel,2*Pi);
                pitch_=std::clamp(pitch_-delta.second*radiansPerPixel,-Pi*0.49,Pi*0.49);
                raw_=FromYawPitch(yaw_,pitch_);
                // One warp per rendered frame prevents a queue of synthetic
                // WM_MOUSEMOVE events from repeatedly amplifying remote input.
                if(captured_&&GetForegroundWindow()==window_)CenterCursor();
            }
        }
        if (udpMode_ && udp_.Poll(raw_)) udpTimeMs_=now;
        const Quaternion orientation=Multiply(center_,raw_);
        memory_.WithLock([&](SharedState& state){
            state.viewerPid=GetCurrentProcessId(); state.orientation=orientation;
            state.poseTimeMs=udpMode_ ? udpTimeMs_ : now;
        });
        SharedState state{};
        const bool snapshot=memory_.Snapshot(state);
        if (snapshot) {
            try {ReceiveFrame(state);} catch (const std::exception& e) {
                lastTextureError_=e.what(); frames_.Disconnect();
            }
        }
        if(snapshot){
            const bool allowFocus=trackingEnabled_&&!desktopParked_&&!IsIconic(window_);
            if(startup_)startup_->Tick(window_,state,allowFocus);
            if(startup_&&startup_->TakeReconnect()){
                trackingEnabled_=true;RestoreLocalPreview();ShowWindow(window_,SW_RESTORE);FocusStreamingWindow(window_);
                captureResetPending_=true;
            }
            if(library_)library_->Tick(window_,state,allowFocus);
        }
        if(!selfTest_&&trackingEnabled_&&!desktopParked_&&!IsIconic(window_)&&state.driverPid&&now>=nextFocusCheck_){
            nextFocusCheck_=now+500;DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
            if(foregroundPid&&foregroundPid!=GetCurrentProcessId()&&
                ((library_&&foregroundPid==library_->SceneProcess())||ProcessInside(foregroundPid,runtimeRoot_)))FocusStreamingWindow(window_);
        }
        if (now-lastTitleMs_>500) {
            std::wstring status=startup_?startup_->Status():L"waiting for SteamVR";
            if (!lastTextureError_.empty()) status=L"frame error - check GPU / restart SteamVR";
            else if (state.status==DriverStatus::GraphicsError) status=L"driver GPU error - see SteamVR log";
            else if (frames_.ReceivedAt() && now-frames_.ReceivedAt()<1000) status=L"SteamVR streaming";
            else if(frames_.HasFrame())status=L"Waiting for frames — last image retained";
            if (udpMode_ && (!udpTimeMs_ || now-udpTimeMs_>PoseTimeoutMs)) status+=L" | UDP tracking lost";
            const std::wstring title=L"FreeLinkVR | "+status+
                (udpMode_ ? L" | UDP 3DoF" : !trackingEnabled_ ? L" | tracking paused (click/M)" :
                    captured_ ? L" | tracking on, mouse captured" : L" | tracking on, mouse free")+
                (monitorError_.empty()?L"":L" | local preview unavailable (F8: stereo)")+
                L" | PC: "+std::to_wstring(static_cast<int>(settings_.monitorScale*100))+L"% (+/-) | F8: monitor  L: library  R: recenter  Esc: desktop";
            SetWindowTextW(window_,title.c_str()); lastTitleMs_=now;
            if (!lastTextureError_.empty()) OutputDebugStringA(lastTextureError_.c_str());
        }
        if(selfTest_&&testFrames_==3){
            auto captureSurface=swapchain_.Get();
            HandleMessage(WM_SYSCOMMAND,SC_MINIMIZE,0);
            if(!desktopParked_||IsIconic(window_))throw std::runtime_error("Local minimize interrupted the capture window");
            HandleMessage(WM_SYSCOMMAND,SC_RESTORE,0);
            if(desktopParked_||swapchain_.Get()!=captureSurface)throw std::runtime_error("Local restore replaced the capture surface");
        }
        if(selfTest_&&testFrames_==4){
            // Exercise the window-resume path even when Remote Play retains
            // the same session ID. GPU pixels are checked again at frame six.
            HandleMessage(WM_SIZE,SIZE_MINIMIZED,0);
            HandleMessage(WM_SIZE,SIZE_RESTORED,0);
            if(!captureResetPending_)throw std::runtime_error("Window restore did not schedule capture recovery");
        }
        if(captureResetPending_&&!IsIconic(window_)){
            const auto preview=monitor_?monitor_->Window():nullptr;
            // Preserve the swap chain registered with Steam's capture hook.
            // Destroying and replacing it while hooked can crash inside DXGI.
            // ResizeBuffers refreshes the capture buffers on the existing chain.
            gpu_.context->OMSetRenderTargets(0,nullptr,nullptr);target_.Reset();
            gpu_.context->Flush();
            Check(swapchain_->ResizeBuffers(0,0,0,DXGI_FORMAT_UNKNOWN,0),"Refresh capture buffers");
            captureResetPending_=false;
            if(selfTest_&&monitor_&&monitor_->Window()!=preview)throw std::runtime_error("Capture recovery replaced the monitor output");
            if(!selfTest_&&trackingEnabled_)FocusStreamingWindow(window_);
            LogWindowEvent("capture buffers refreshed");
        }
        DXGI_SWAP_CHAIN_DESC swapDesc{}; Check(swapchain_->GetDesc(&swapDesc),"Get swap chain description");
        RECT rect{}; GetClientRect(window_,&rect);
        if(IsIconic(window_)){rect.right=static_cast<LONG>(swapDesc.BufferDesc.Width);rect.bottom=static_cast<LONG>(swapDesc.BufferDesc.Height);}
        const UINT width=static_cast<UINT>(rect.right),height=static_cast<UINT>(rect.bottom);
        if (!width || !height) return;
        if (!target_ || swapDesc.BufferDesc.Width!=width || swapDesc.BufferDesc.Height!=height) {
            gpu_.context->OMSetRenderTargets(0,nullptr,nullptr); target_.Reset();
            Check(swapchain_->ResizeBuffers(0,width,height,DXGI_FORMAT_UNKNOWN,0),"Resize swap chain");
            ComPtr<ID3D11Texture2D> backbuffer; Check(swapchain_->GetBuffer(0,IID_PPV_ARGS(&backbuffer)),"Get window buffer");
            Check(gpu_.device->CreateRenderTargetView(backbuffer.Get(),nullptr,&target_),"Create window target");
        }
        const float black[]={0,0,0,1};
        gpu_.context->ClearRenderTargetView(target_.Get(),black);
        float aspect=16.0f/9;
        if (frames_.Texture()) {D3D11_TEXTURE2D_DESC desc{};frames_.Texture()->GetDesc(&desc);aspect=static_cast<float>(desc.Width)/desc.Height;}
        const float viewWidth=std::min(static_cast<float>(width),height*aspect);
        const float viewHeight=viewWidth/aspect;
        D3D11_VIEWPORT viewport{(width-viewWidth)/2,(height-viewHeight)/2,viewWidth,viewHeight,0,1};
        gpu_.context->RSSetViewports(1,&viewport);
        ID3D11RenderTargetView* target=target_.Get();
        gpu_.context->OMSetRenderTargets(1,&target,nullptr);
        gpu_.context->IASetInputLayout(nullptr);
        gpu_.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        gpu_.context->VSSetShader(vertexShader_.Get(),nullptr,0);
        const bool live=frames_.HasFrame();
        gpu_.context->PSSetShader(live ? pixelShader_.Get() : gridShader_.Get(),nullptr,0);
        ID3D11ShaderResourceView* image=frames_.View();
        gpu_.context->PSSetShaderResources(0,1,&image);
        ID3D11SamplerState* sampler=sampler_.Get(); gpu_.context->PSSetSamplers(0,1,&sampler);
        auto optics=optics_.Get();gpu_.context->PSSetConstantBuffers(0,1,&optics);
        gpu_.context->Draw(3,0);
        if(selfTest_)++testFrames_;
        if(selfTest_&&(testFrames_==2||testFrames_==6)) {
            ComPtr<ID3D11Texture2D> backbuffer;Check(swapchain_->GetBuffer(0,IID_PPV_ARGS(&backbuffer)),"Read lens preview");
            D3D11_TEXTURE2D_DESC desc{};backbuffer->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> staging;Check(gpu_.device->CreateTexture2D(&desc,nullptr,&staging),"Create preview readback");gpu_.context->CopyResource(staging.Get(),backbuffer.Get());
            D3D11_MAPPED_SUBRESOURCE data{};Check(gpu_.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&data),"Map lens preview");
            std::vector<unsigned char> pixels(static_cast<size_t>(desc.Width)*desc.Height*4);
            for(UINT y=0;y<desc.Height;++y)for(UINT x=0;x<desc.Width;++x){const auto src=static_cast<const unsigned char*>(data.pData)+y*data.RowPitch+x*4;auto dst=pixels.data()+(static_cast<size_t>(y)*desc.Width+x)*4;dst[0]=src[2];dst[1]=src[1];dst[2]=src[0];dst[3]=255;}
            gpu_.context->Unmap(staging.Get(),0);
            const size_t left=(static_cast<size_t>(desc.Height/2)*desc.Width+desc.Width/4)*4,right=(static_cast<size_t>(desc.Height/2)*desc.Width+desc.Width*3/4)*4;
            if(pixels[left]<=pixels[left+2]||pixels[right+2]<=pixels[right])throw std::runtime_error("Stereo capture source lost an eye during local monitor rendering");
            WriteBitmap(std::filesystem::current_path()/L"lens-preview.bmp",desc.Width,desc.Height,pixels);
        }
        const HRESULT present=swapchain_->Present(0,0);
        if(present!=lastPresent_){
            const auto event="present="+std::to_string(static_cast<unsigned long>(present))+" frames="+std::to_string(state.frames)+" sourceAgeMs="+std::to_string(frames_.ReceivedAt()?now-frames_.ReceivedAt():0);
            LogWindowEvent(event.c_str());lastPresent_=present;
        }
        Check(present,"Present window");
        // Present the stereo swap chain first: this is Steam's capture source.
        // The independent local compositor never overwrites that backbuffer.
        if(monitorEnabled_&&(selfTest_||trackingEnabled_)){
          if(now>=monitorRetryAt_&&(selfTest_||now>=nextMonitorFrameAt_)){
            try{
                if(!monitor_){monitor_=std::make_unique<MonitorView>(gpu_);monitor_->Open(window_);}
                const auto preview=selfTest_&&testFrames_==2?std::filesystem::current_path()/L"monitor-preview.bmp":std::filesystem::path{};
                monitor_->Draw(!selfTest_,[&](ID3D11RenderTargetView* rtv,UINT w,UINT h,POINT offset){DrawMonitor(rtv,w,h,offset);},preview,!desktopParked_);monitorError_.clear();nextMonitorFrameAt_=now+33;
            }catch(const std::exception& e){if(selfTest_)throw;monitorError_=e.what();monitor_.reset();monitorRetryAt_=now+5000;}
          }
        }else if(monitor_)monitor_->Hide();
    }
    void DrawMonitor(ID3D11RenderTargetView* rtv,UINT width,UINT height,POINT offset){
        ID3D11ShaderResourceView* image=library_?library_->MonitorFrame():nullptr;
        const bool nativeMirror=image!=nullptr;
        if(!image)image=frames_.View();
        float eyeAspect=8.0f/9;bool encodeSRGB=false;
        if(image){D3D11_SHADER_RESOURCE_VIEW_DESC desc{};image->GetDesc(&desc);encodeSRGB=desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB||desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB||desc.Format==DXGI_FORMAT_R16G16B16A16_FLOAT||desc.Format==DXGI_FORMAT_R32G32B32A32_FLOAT;}
        if(image){ComPtr<ID3D11Resource> resource;image->GetResource(&resource);ComPtr<ID3D11Texture2D> texture;if(SUCCEEDED(resource.As(&texture))){D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);eyeAspect=static_cast<float>(desc.Width)*(nativeMirror?1.f:.5f)/desc.Height;}}
        constexpr float monitorAspect=16.f/9;
        const float crop[]{std::min(1.f,monitorAspect/eyeAspect),std::min(1.f,eyeAspect/monitorAspect),nativeMirror?1.f:.5f,nativeMirror?.5f:.25f,encodeSRGB?1.f:0.f,0,0,0};
        gpu_.context->UpdateSubresource(monoOptions_.Get(),0,nullptr,crop,0,0);
        const float viewWidth=std::min(static_cast<float>(width),height*monitorAspect)*settings_.monitorScale,viewHeight=viewWidth/monitorAspect;
        D3D11_VIEWPORT view{offset.x+(width-viewWidth)*.5f,offset.y+(height-viewHeight)*.5f,viewWidth,viewHeight,0,1};gpu_.context->RSSetViewports(1,&view);
        gpu_.context->OMSetRenderTargets(1,&rtv,nullptr);gpu_.context->IASetInputLayout(nullptr);gpu_.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        gpu_.context->VSSetShader(vertexShader_.Get(),nullptr,0);gpu_.context->PSSetShader(image?monoShader_.Get():monoGridShader_.Get(),nullptr,0);
        gpu_.context->PSSetShaderResources(0,1,&image);auto sampler=sampler_.Get();gpu_.context->PSSetSamplers(0,1,&sampler);auto options=monoOptions_.Get();gpu_.context->PSSetConstantBuffers(0,1,&options);gpu_.context->Draw(3,0);
    }
    void TestMonitorColor(){
        D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=2;desc.MipLevels=desc.ArraySize=1;desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        const uint32_t gray[]{0xff808080,0xff808080,0xff808080,0xff808080};D3D11_SUBRESOURCE_DATA data{gray,8,0};ComPtr<ID3D11Texture2D> source;
        Check(gpu_.device->CreateTexture2D(&desc,&data,&source),"Create color test source");
        desc.Width=desc.Height=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BindFlags=D3D11_BIND_RENDER_TARGET;ComPtr<ID3D11Texture2D> output;Check(gpu_.device->CreateTexture2D(&desc,nullptr,&output),"Create color test output");
        ComPtr<ID3D11RenderTargetView> target;Check(gpu_.device->CreateRenderTargetView(output.Get(),nullptr,&target),"Create color test target");
        desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> readback;Check(gpu_.device->CreateTexture2D(&desc,nullptr,&readback),"Create color test readback");
        for(bool srgb:{false,true}){
            D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=srgb?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:DXGI_FORMAT_R8G8B8A8_UNORM;view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;view.Texture2D.MipLevels=1;
            ComPtr<ID3D11ShaderResourceView> image;Check(gpu_.device->CreateShaderResourceView(source.Get(),&view,&image),"Create color test view");
            const float constants[]{1,1,1,.5f,srgb?1.f:0.f,0,0,0};gpu_.context->UpdateSubresource(monoOptions_.Get(),0,nullptr,constants,0,0);
            const D3D11_VIEWPORT viewport{0,0,1,1,0,1};gpu_.context->RSSetViewports(1,&viewport);auto rtv=target.Get();gpu_.context->OMSetRenderTargets(1,&rtv,nullptr);
            gpu_.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);gpu_.context->VSSetShader(vertexShader_.Get(),nullptr,0);gpu_.context->PSSetShader(monoShader_.Get(),nullptr,0);
            auto srv=image.Get();gpu_.context->PSSetShaderResources(0,1,&srv);auto sampler=sampler_.Get();gpu_.context->PSSetSamplers(0,1,&sampler);auto buffer=monoOptions_.Get();gpu_.context->PSSetConstantBuffers(0,1,&buffer);gpu_.context->Draw(3,0);
            gpu_.context->OMSetRenderTargets(0,nullptr,nullptr);gpu_.context->CopyResource(readback.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE pixels{};Check(gpu_.context->Map(readback.Get(),0,D3D11_MAP_READ,0,&pixels),"Read color test result");
            const auto value=*static_cast<const unsigned char*>(pixels.pData);gpu_.context->Unmap(readback.Get(),0);
            if(std::abs(static_cast<int>(value)-128)>2)throw std::runtime_error("Monitor sRGB conversion darkens or double-encodes the image");
        }
        gpu_.context->ClearState();
    }
};
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    SetProcessDPIAware();
    Options options;
    try {options=ParseOptions();}
    catch (const std::exception& e) {MessageBoxA(nullptr,e.what(),"FreeLinkVR error",MB_OK|MB_ICONERROR);return 1;}
    try {if(const auto bootstrap=slvr::BootstrapInputContext())return *bootstrap;}
    catch(const std::exception& e){MessageBoxA(nullptr,e.what(),"FreeLinkVR launch error",MB_OK|MB_ICONERROR);return 1;}
    HANDLE singleInstance=CreateMutexW(nullptr,FALSE,slvr::ViewerMutexName);
    if (!singleInstance) return 1;
    if (GetLastError()==ERROR_ALREADY_EXISTS) {
        if(options.inputDiagnostics) {
            // Forward to the existing Steam-launched process, preserving its
            // application identity, input profile and overlay association.
            HWND viewer=FindWindowW(L"FreeLinkVRViewer",nullptr);
            if(viewer && PostMessageW(viewer,slvr::RequestInputDiagnostics,0,0)) {
                CloseHandle(singleInstance);return 0;
            }
        }
        HWND existing=FindWindowW(L"FreeLinkVRViewer",nullptr);
        if(!existing)existing=FindWindowW(L"FreeLinkVRInputDiagnostics",nullptr);
        if(existing){ShowWindow(existing,SW_RESTORE);SetForegroundWindow(existing);}
        CloseHandle(singleInstance); return existing?0:1;
    }
    int result=0;
    try {
      if(!options.inputDiagnostics) {
        Viewer viewer;
        WNDCLASSW wc{}; wc.lpfnWndProc=Viewer::WindowProc; wc.hInstance=instance;
        wc.lpszClassName=L"FreeLinkVRViewer"; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        if (!RegisterClassW(&wc)) throw std::runtime_error("Cannot register viewer window class");
        HWND window=CreateWindowExW(0,wc.lpszClassName,L"FreeLinkVR",WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,CW_USEDEFAULT,1280,760,nullptr,nullptr,instance,&viewer);
        if (!window) throw std::runtime_error("Cannot create viewer window");
        viewer.Create(window,options);
        if (!options.selfTest) {
            ShowWindow(window,show);
            if(options.fullscreen || UserSettings::Load().fullscreen)viewer.ToggleFullscreen();
            if(options.capture && options.address.empty())viewer.Capture(true);
        }
        MSG message{}; bool running=true;
        const auto epoch=std::chrono::steady_clock::now();auto nextFrame=epoch;
        int testFrames=0;
        while (running) {
            // Limit messages per render iteration so injected mouse traffic cannot starve frames.
            for (int i=0;i<256 && PeekMessageW(&message,nullptr,0,0,PM_REMOVE);++i) {
                if (message.message==WM_QUIT) {running=false; break;}
                TranslateMessage(&message); DispatchMessageW(&message);
            }
            const auto beforeFrame=std::chrono::steady_clock::now();
            if(running&&(options.selfTest||beforeFrame>=nextFrame)){
                nextFrame=NextVsync(epoch,beforeFrame,viewer.FramesPerSecond());viewer.Tick();
                if(options.selfTest&&++testFrames>=16)running=false;
            }
            if(running&&!options.selfTest){
                const auto now=std::chrono::steady_clock::now();
                if(nextFrame>now){const double milliseconds=std::chrono::duration<double,std::milli>(nextFrame-now).count();const DWORD wait=static_cast<DWORD>(std::clamp(std::ceil(milliseconds),1.0,1000.0));MsgWaitForMultipleObjects(0,nullptr,FALSE,wait,QS_ALLINPUT);}
            }
        }
        options.inputDiagnostics=viewer.DiagnosticsRequested();
      }
      if(options.inputDiagnostics)
          result=slvr::RunInputDiagnostics(instance,show,options.steamApi,options.diagnosticSeconds,options.selfTest);
    } catch (const std::exception& e) {
        if (options.selfTest) {
            const std::string error=std::string("FreeLinkVR self-test failed: ")+e.what()+"\n";
            OutputDebugStringA(error.c_str());
            DWORD written=0;
            WriteFile(GetStdHandle(STD_ERROR_HANDLE),error.data(),static_cast<DWORD>(error.size()),&written,nullptr);
        } else MessageBoxA(nullptr,e.what(),"FreeLinkVR error",MB_OK|MB_ICONERROR);
        result=1;
    }
    CloseHandle(singleInstance);
    return result;
}
