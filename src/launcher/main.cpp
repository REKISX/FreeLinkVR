#include <windows.h>
#include <commctrl.h>
#include <future>
#include <string>
#include "platform/setup.h"
#include "platform/settings.h"
#include "common/image_dump.h"

namespace {
using namespace slvr;
HWND Control(HWND parent,const wchar_t* type,const wchar_t* text,DWORD style,int x,int y,int width,int height,int id,HFONT font){
    HWND control=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,x,y,width,height,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return control;
}
struct SettingsPanel {
    HWND window=nullptr,resolution=nullptr,fps=nullptr,ipd=nullptr,lenses=nullptr,strength=nullptr,speed=nullptr,full=nullptr,monitor=nullptr;
    HWND strengthValue=nullptr,speedValue=nullptr,monitorScale=nullptr,monitorValue=nullptr;HFONT font=nullptr;UserSettings settings=UserSettings::Load();
    void UpdateLabels(){SetWindowTextW(strengthValue,(std::to_wstring(SendMessageW(strength,TBM_GETPOS,0,0))+L"%").c_str());SetWindowTextW(speedValue,(std::to_wstring(SendMessageW(speed,TBM_GETPOS,0,0))+L"%").c_str());SetWindowTextW(monitorValue,(std::to_wstring(SendMessageW(monitorScale,TBM_GETPOS,0,0))+L"%").c_str());}
    void Create(){
        Control(window,L"STATIC",L"Image quality",0,24,22,260,25,0,font);
        resolution=Control(window,WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP,290,18,150,100,10,font);
        SendMessageW(resolution,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"1280 × 720"));SendMessageW(resolution,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"1920 × 1080"));SendMessageW(resolution,CB_SETCURSEL,settings.width==1920?1:0,0);
        Control(window,L"STATIC",L"Frame rate",0,24,62,250,25,0,font);
        fps=Control(window,WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP,290,58,150,100,11,font);
        SendMessageW(fps,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"60 FPS"));SendMessageW(fps,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"30 FPS"));SendMessageW(fps,CB_SETCURSEL,settings.fps==30?1:0,0);
        Control(window,L"STATIC",L"Interpupillary distance, mm",0,24,104,280,25,0,font);
        ipd=Control(window,L"EDIT",std::to_wstring(settings.ipdMm).c_str(),WS_BORDER|ES_NUMBER|WS_TABSTOP,335,100,105,29,12,font);
        lenses=Control(window,L"BUTTON",L"Lens distortion correction",BS_AUTOCHECKBOX|WS_TABSTOP,24,148,420,28,13,font);SendMessageW(lenses,BM_SETCHECK,settings.lenses?BST_CHECKED:BST_UNCHECKED,0);
        strength=Control(window,TRACKBAR_CLASSW,L"",TBS_AUTOTICKS|WS_TABSTOP,24,182,350,36,14,font);SendMessageW(strength,TBM_SETRANGE,TRUE,MAKELPARAM(0,60));SendMessageW(strength,TBM_SETPOS,TRUE,static_cast<int>(settings.lensStrength*100));
        strengthValue=Control(window,L"STATIC",L"",0,380,187,65,25,0,font);
        Control(window,L"STATIC",L"Turn sensitivity",0,24,232,300,25,0,font);
        speed=Control(window,TRACKBAR_CLASSW,L"",TBS_AUTOTICKS|WS_TABSTOP,24,262,350,36,15,font);SendMessageW(speed,TBM_SETRANGE,TRUE,MAKELPARAM(25,200));SendMessageW(speed,TBM_SETPOS,TRUE,static_cast<int>(settings.sensitivity*100));
        speedValue=Control(window,L"STATIC",L"",0,380,267,65,25,0,font);
        full=Control(window,L"BUTTON",L"Start in fullscreen",BS_AUTOCHECKBOX|WS_TABSTOP,24,316,420,28,16,font);SendMessageW(full,BM_SETCHECK,settings.fullscreen?BST_CHECKED:BST_UNCHECKED,0);
        monitor=Control(window,L"BUTTON",L"Monitor: one eye, 16:9 (F8)",BS_AUTOCHECKBOX|WS_TABSTOP,24,348,425,28,17,font);SendMessageW(monitor,BM_SETCHECK,settings.monitorPreview?BST_CHECKED:BST_UNCHECKED,0);
        Control(window,L"STATIC",L"PC preview size",0,24,385,425,27,0,font);
        monitorScale=Control(window,TRACKBAR_CLASSW,L"",TBS_AUTOTICKS|WS_TABSTOP,24,416,350,36,18,font);SendMessageW(monitorScale,TBM_SETRANGE,TRUE,MAKELPARAM(50,100));SendMessageW(monitorScale,TBM_SETPOS,TRUE,static_cast<int>(settings.monitorScale*100));
        monitorValue=Control(window,L"STATIC",L"",0,380,421,65,25,0,font);
        Control(window,L"STATIC",L"Use +/− to resize the PC preview.\nSettings apply on the next launch.",0,24,462,425,48,0,font);
        Control(window,L"BUTTON",L"Save",BS_DEFPUSHBUTTON|WS_TABSTOP,245,525,110,36,IDOK,font);Control(window,L"BUTTON",L"Cancel",WS_TABSTOP,365,525,90,36,IDCANCEL,font);UpdateLabels();
    }
    void Save(){
        settings.width=SendMessageW(resolution,CB_GETCURSEL,0,0)==1?1920:1280;settings.height=settings.width==1920?1080:720;
        settings.fps=SendMessageW(fps,CB_GETCURSEL,0,0)==1?30:60;
        wchar_t value[24]{};GetWindowTextW(ipd,value,24);const int distance=_wtoi(value);if(distance<55||distance>75){MessageBoxW(window,L"Enter a distance between 55 and 75 mm.",L"Settings",MB_OK);return;}
        settings.ipdMm=distance;settings.lensStrength=static_cast<float>(SendMessageW(strength,TBM_GETPOS,0,0))/100;settings.sensitivity=static_cast<float>(SendMessageW(speed,TBM_GETPOS,0,0))/100;
        settings.lenses=SendMessageW(lenses,BM_GETCHECK,0,0)==BST_CHECKED;settings.fullscreen=SendMessageW(full,BM_GETCHECK,0,0)==BST_CHECKED;settings.monitorPreview=SendMessageW(monitor,BM_GETCHECK,0,0)==BST_CHECKED;settings.monitorScale=static_cast<float>(SendMessageW(monitorScale,TBM_GETPOS,0,0))/100;settings.Save();DestroyWindow(window);
    }
    static LRESULT CALLBACK Proc(HWND window,UINT message,WPARAM w,LPARAM l){
        auto self=reinterpret_cast<SettingsPanel*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE){self=static_cast<SettingsPanel*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(window,message,w,l);
        if(message==WM_CTLCOLORSTATIC){SetBkMode(reinterpret_cast<HDC>(w),TRANSPARENT);return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));}
        try{if(message==WM_CREATE){self->Create();return 0;}if(message==WM_HSCROLL){self->UpdateLabels();return 0;}if(message==WM_COMMAND){if(LOWORD(w)==IDOK){self->Save();return 0;}if(LOWORD(w)==IDCANCEL){DestroyWindow(window);return 0;}}if(message==WM_CLOSE){DestroyWindow(window);return 0;}}
        catch(const std::exception& e){MessageBoxW(window,Wide(e.what()).c_str(),L"FreeLinkVR",MB_OK|MB_ICONERROR);}return DefWindowProcW(window,message,w,l);
    }
};
void ShowSettings(HWND parent,HFONT font){
    SettingsPanel panel;panel.font=font;WNDCLASSW cls{};cls.lpfnWndProc=SettingsPanel::Proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"FreeLinkVRSettings";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&cls);
    RECT rect{};GetWindowRect(parent,&rect);HWND window=CreateWindowExW(WS_EX_DLGMODALFRAME,cls.lpszClassName,L"FreeLinkVR — Settings",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,rect.left+20,rect.top+10,490,615,parent,nullptr,cls.hInstance,&panel);
    if(!window)throw std::runtime_error("Cannot create settings window");EnableWindow(parent,FALSE);ShowWindow(window,SW_SHOW);MSG message{};
    while(IsWindow(window)&&GetMessageW(&message,nullptr,0,0)>0)if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}EnableWindow(parent,TRUE);SetForegroundWindow(parent);
}
struct Launcher {
    HWND window=nullptr,status=nullptr,setup=nullptr,start=nullptr,settingsButton=nullptr;HFONT font=nullptr,heading=nullptr;
    std::future<SetupResult> work;bool busy=false,downloadPending=false;std::filesystem::path package=AppDirectory();
    ~Launcher(){if(font)DeleteObject(font);if(heading)DeleteObject(heading);}
    void Refresh(){SetWindowTextW(setup,IsSetupComplete(package)?L"Reinstall":L"Setup");}
    void Begin(bool install){
        if(busy)return;busy=true;downloadPending=false;EnableWindow(setup,FALSE);EnableWindow(start,FALSE);EnableWindow(settingsButton,FALSE);
        SetWindowTextW(status,install?L"Setting up FreeLinkVR… Steam may restart.":L"Preparing Steam and starting the stream…");
        const auto directory=package;work=std::async(std::launch::async,[directory,install]{
            if(install)return Install(directory);try{StartSession(directory);return SetupResult{true,false,L"Connect your phone with Steam Link. The stream will start SteamVR after the phone connects."};}catch(const std::exception& e){return SetupResult{false,false,Wide(e.what())};}
        });
    }
    void Tick(){
        if(busy && work.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){const auto result=work.get();busy=false;downloadPending=result.waitingForDownload;SetWindowTextW(status,result.message.c_str());EnableWindow(setup,TRUE);EnableWindow(start,TRUE);EnableWindow(settingsButton,TRUE);Refresh();if(result.complete)SetWindowTextW(setup,L"Reinstall");}
        if(downloadPending){try{if(!FindSteamVR().empty()){downloadPending=false;Begin(true);}}catch(...){} }
    }
    void Create(){
        font=CreateFontW(-18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        heading=CreateFontW(-32,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        Control(window,L"STATIC",L"FreeLinkVR",0,28,20,430,45,0,heading);Control(window,L"STATIC",L"Phone VR through Steam Link",0,30,68,435,28,0,font);
        settingsButton=Control(window,L"BUTTON",L"Settings",WS_TABSTOP,28,114,430,48,100,font);
        setup=Control(window,L"BUTTON",L"Setup",WS_TABSTOP,28,174,430,48,101,font);
        start=Control(window,L"BUTTON",L"Start",BS_DEFPUSHBUTTON|WS_TABSTOP,28,234,430,52,102,font);
        status=Control(window,L"STATIC",L"First run: choose Setup. Enable gyro-to-mouse in the Steam Link controller layout.",0,28,309,430,76,0,font);Refresh();if(IsSetupComplete(package))SetWindowTextW(status,L"Ready. Choose Start, then connect your phone with Steam Link.");SetTimer(window,1,500,nullptr);
    }
    static LRESULT CALLBACK Proc(HWND window,UINT message,WPARAM w,LPARAM l){
        auto self=reinterpret_cast<Launcher*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE){self=static_cast<Launcher*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(window,message,w,l);
        if(message==WM_CTLCOLORSTATIC){SetBkMode(reinterpret_cast<HDC>(w),TRANSPARENT);return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));}
        try{
            if(message==WM_CREATE){self->Create();return 0;}
            if(message==WM_COMMAND){switch(LOWORD(w)){case 100:ShowSettings(window,self->font);return 0;case 101:self->Begin(true);return 0;case 102:self->Begin(false);return 0;}}
            if(message==WM_TIMER){self->Tick();return 0;}
            if(message==WM_CLOSE){if(self->busy){SetWindowTextW(self->status,L"Wait for the current operation to finish.");return 0;}DestroyWindow(window);return 0;}
            if(message==WM_DESTROY){KillTimer(window,1);PostQuitMessage(0);return 0;}
        }catch(const std::exception& e){MessageBoxW(window,Wide(e.what()).c_str(),L"FreeLinkVR",MB_OK|MB_ICONERROR);}return DefWindowProcW(window,message,w,l);
    }
};
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR command,int show){
    SetProcessDPIAware();INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_BAR_CLASSES};InitCommonControlsEx(&controls);
    HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\FreeLinkVR.Launcher");if(!mutex)return 1;
    if(GetLastError()==ERROR_ALREADY_EXISTS){if(HWND existing=FindWindowW(L"FreeLinkVRLauncher",nullptr)){ShowWindow(existing,SW_RESTORE);SetForegroundWindow(existing);}CloseHandle(mutex);return 0;}
    int result=0;
    try{Launcher launcher;WNDCLASSW cls{};cls.lpfnWndProc=Launcher::Proc;cls.hInstance=instance;cls.lpszClassName=L"FreeLinkVRLauncher";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&cls);
        HWND window=CreateWindowExW(0,cls.lpszClassName,L"FreeLinkVR",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,505,435,nullptr,nullptr,instance,&launcher);if(!window)throw std::runtime_error("Cannot create launcher");
        if(!wcscmp(command,L"--self-test")){
            ShowWindow(window,SW_SHOWNOACTIVATE);UpdateWindow(window);
            CaptureOwnWindow(window,std::filesystem::current_path()/L"launcher-preview.bmp");
            SettingsPanel panel;panel.font=launcher.font;WNDCLASSW settingsClass{};settingsClass.lpfnWndProc=SettingsPanel::Proc;settingsClass.hInstance=instance;settingsClass.lpszClassName=L"FreeLinkVRSettings";settingsClass.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&settingsClass);
            HWND settings=CreateWindowExW(0,settingsClass.lpszClassName,L"FreeLinkVR",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,0,0,490,615,window,nullptr,instance,&panel);
            if(!settings)throw std::runtime_error("Cannot create settings preview");ShowWindow(settings,SW_SHOWNOACTIVATE);UpdateWindow(settings);CaptureOwnWindow(settings,std::filesystem::current_path()/L"settings-preview.bmp");DestroyWindow(settings);DestroyWindow(window);
        }else ShowWindow(window,show);MSG message{};
        while(GetMessageW(&message,nullptr,0,0)>0){if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
    }catch(const std::exception& e){MessageBoxW(nullptr,Wide(e.what()).c_str(),L"FreeLinkVR",MB_OK|MB_ICONERROR);result=1;}CloseHandle(mutex);return result;
}
