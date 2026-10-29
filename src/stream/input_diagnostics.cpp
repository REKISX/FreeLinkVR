#include "platform/steam_input.h"
#include "stream/input_diagnostics.h"
#include <Xinput.h>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace slvr {
namespace {
struct Diagnostics {
    HWND window=nullptr;
    SteamInput input;
    std::wstring text;
    std::string failure;
    uint64_t mouseEvents=0,rawEvents=0,keyEvents=0,changed=0;
    uint64_t lastChange=0,lastLog=0,start=GetTickCount64();
    SteamMotion previous{};
    uint64_t previousHandle=0;
    bool havePrevious=false,everSensor=false;
    std::ofstream log;
    static LRESULT CALLBACK Proc(HWND window,UINT message,WPARAM w,LPARAM l) {
        auto self=reinterpret_cast<Diagnostics*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE) {
            self=static_cast<Diagnostics*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            self->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if(!self)return DefWindowProcW(window,message,w,l);
        switch(message) {
        case WM_DESTROY:self->window=nullptr;PostQuitMessage(0);return 0;
        case WM_KEYDOWN:++self->keyEvents;break;
        case WM_MOUSEMOVE:++self->mouseEvents;break;
        case WM_INPUT:++self->rawEvents;break;
        case WM_PAINT: {
            PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);RECT rect{};GetClientRect(window,&rect);
            FillRect(dc,&rect,reinterpret_cast<HBRUSH>(COLOR_WINDOW+1));
            rect.left+=16;rect.top+=12;rect.right-=16;
            SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(20,20,20));
            const auto old=SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT));
            DrawTextW(dc,self->text.c_str(),-1,&rect,DT_LEFT|DT_TOP|DT_WORDBREAK|DT_NOPREFIX);
            SelectObject(dc,old);EndPaint(window,&paint);return 0;
        }
        }
        return DefWindowProcW(window,message,w,l);
    }
    void Tick() {
        std::vector<SteamDevice> devices;
        if(failure.empty()) {
            try {devices=input.Poll();}catch(const std::exception& e){failure=e.what();input.Close();}
        }
        const uint64_t now=GetTickCount64();
        std::wostringstream textOut;textOut<<std::fixed<<std::setprecision(5);
        std::ostringstream logOut;logOut<<std::fixed<<std::setprecision(6);
        textOut<<L"FreeLinkVR — Input diagnostics (no SteamVR or mouse capture)\n\n";
        if(!failure.empty())textOut<<L"Connection error: "<<std::wstring(failure.begin(),failure.end())<<L"\n";
        else textOut<<L"Steam Input: connected. Controllers: "<<devices.size()<<L"\n";
        textOut<<L"Window events — mouse: "<<mouseEvents<<L", Raw Input: "<<rawEvents<<L", keys: "<<keyEvents<<L"\n";
        DWORD pads=0;WORD buttons=0;
        for(DWORD i=0;i<XUSER_MAX_COUNT;++i){XINPUT_STATE state{};if(XInputGetState(i,&state)==ERROR_SUCCESS){++pads;buttons|=state.Gamepad.wButtons;}}
        textOut<<L"XInput gamepads: "<<pads<<L", buttons: "<<buttons<<L"\n\n";
        bool found=false;
        for(const auto& device:devices) {
            textOut<<L"Controller "<<device.handle<<L": type "<<device.type<<(device.mobile()?L" (MobileTouch — phone)":L"")<<L"\n";
            if(!device.mobile() || found)continue;
            found=true;
            const auto& m=device.motion;
            const bool valid=FiniteMotion(m);
            const bool evidence=HasSensorEvidence(m);
            if(!havePrevious || previousHandle!=device.handle){havePrevious=false;changed=0;lastChange=0;}
            if(valid && havePrevious) {
                const float differences[]={m.qx-previous.qx,m.qy-previous.qy,m.qz-previous.qz,m.qw-previous.qw,
                    m.accelX-previous.accelX,m.accelY-previous.accelY,m.accelZ-previous.accelZ,
                    m.gyroX-previous.gyroX,m.gyroY-previous.gyroY,m.gyroZ-previous.gyroZ};
                bool different=false;for(float delta:differences)if(std::abs(delta)>1e-5f)different=true;
                if(different){++changed;lastChange=now;}
            }
            if(valid){previous=m;havePrevious=true;previousHandle=device.handle;}
            everSensor=everSensor || (evidence && changed>0);
            textOut<<L"Quaternion x/y/z/w: "<<m.qx<<L"  "<<m.qy<<L"  "<<m.qz<<L"  "<<m.qw<<L"\n"
                <<L"Gyroscope x/y/z: "<<m.gyroX<<L"  "<<m.gyroY<<L"  "<<m.gyroZ<<L"\n"
                <<L"Acceleration x/y/z: "<<m.accelX<<L"  "<<m.accelY<<L"  "<<m.accelZ<<L"\n"
                <<L"Changes: "<<changed<<L"; last change: "<<(lastChange?std::to_wstring(now-lastChange)+L" ms ago":L"not observed")<<L"\n"
                <<L"Sensors: "<<(!valid?L"invalid values":evidence?L"nonzero values — check their response to rotation":L"zero/default data; sensor transmission is unconfirmed")<<L"\n\n";
            logOut<<" mobile="<<device.handle<<" q="<<m.qx<<','<<m.qy<<','<<m.qz<<','<<m.qw
                <<" gyro="<<m.gyroX<<','<<m.gyroY<<','<<m.gyroZ<<" accel="<<m.accelX<<','<<m.accelY<<','<<m.accelZ
                <<" changes="<<changed<<" sensorEvidence="<<evidence;
        }
        if(!found){havePrevious=false;textOut<<L"MobileTouch not found. Connect Steam Link and enable the touch controller.\n\n";}
        textOut<<L"Test:\n1. Switch Steam Link to this window.\n2. Swipe / press a button and watch the counters.\n"
            <<L"3. Rotate the phone without touching the screen. Watch the quaternion, gyroscope and acceleration.\n\n"
            <<L"Mouse movement alone does not prove raw sensor transmission.\nRepeated values do not prove that packets are fresh.\n"
            <<L"Report: input-diagnostics.log next to the executable. Close this window to end the test.";
        text=textOut.str();InvalidateRect(window,nullptr,FALSE);
        if(now-lastLog>=500) {
            if(log)log<<"ms="<<now-start<<" controllers="<<devices.size()<<" mouse="<<mouseEvents<<" raw="<<rawEvents
                <<" keys="<<keyEvents<<" xinput="<<pads<<logOut.str()<<" error="<<failure<<std::endl;
            lastLog=now;
        }
    }
};
}
int RunInputDiagnostics(HINSTANCE instance,int show,const std::filesystem::path& api,int seconds,bool hidden) {
    Diagnostics diag;
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    const auto logPath=std::filesystem::path(executable).parent_path()/L"input-diagnostics.log";
    diag.log.open(logPath,std::ios::trunc);
    try {diag.input.Open(api);}catch(const std::exception& e){diag.failure=e.what();}
    WNDCLASSW wc{};wc.lpfnWndProc=Diagnostics::Proc;wc.hInstance=instance;wc.lpszClassName=L"FreeLinkVRInputDiagnostics";
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    if(!RegisterClassW(&wc))throw std::runtime_error("Cannot register input diagnostics window");
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"FreeLinkVR — Input Diagnostics",WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,1020,650,nullptr,nullptr,instance,&diag);
    if(!window)throw std::runtime_error("Cannot create input diagnostics window");
    RAWINPUTDEVICE mouse{1,2,0,window};RegisterRawInputDevices(&mouse,1,sizeof(mouse));
    if(!hidden)ShowWindow(window,show);
    MSG message{};bool running=true;uint64_t nextPoll=GetTickCount64();
    while(running) {
        for(int i=0;i<256 && PeekMessageW(&message,nullptr,0,0,PM_REMOVE);++i) {
            if(message.message==WM_QUIT){running=false;break;}
            TranslateMessage(&message);DispatchMessageW(&message);
        }
        if(!running)break;
        const uint64_t now=GetTickCount64();
        if(now>=nextPoll){diag.Tick();nextPoll=GetTickCount64()+16;}
        if(seconds>0 && now-diag.start>=static_cast<uint64_t>(seconds)*1000)break;
        const uint64_t after=GetTickCount64();
        const DWORD wait=nextPoll>after?static_cast<DWORD>(nextPoll-after):0;
        MsgWaitForMultipleObjects(0,nullptr,FALSE,wait,QS_ALLINPUT);
    }
    if(diag.window)DestroyWindow(diag.window);
    if(diag.log)diag.log<<"RESULT connected="<<diag.failure.empty()<<" observedChangingSensor="<<diag.everSensor<<std::endl;
    return diag.failure.empty()?0:1;
}
}
