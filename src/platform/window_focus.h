#pragma once
#include <windows.h>
#include <filesystem>
namespace slvr {
inline bool FocusStreamingWindow(HWND window){
    if(!window)return false;
    if(IsIconic(window))ShowWindow(window,SW_RESTORE);
    const HWND foreground=GetForegroundWindow();
    if(foreground==window)return true;
    const DWORD other=foreground?GetWindowThreadProcessId(foreground,nullptr):0,current=GetCurrentThreadId();
    const bool attached=other&&other!=current&&AttachThreadInput(current,other,TRUE);
    SetForegroundWindow(window);SetActiveWindow(window);SetFocus(window);
    if(attached)AttachThreadInput(current,other,FALSE);
    return GetForegroundWindow()==window;
}
inline bool ProcessInside(DWORD pid,const std::filesystem::path& directory){
    if(!pid||directory.empty())return false;
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!process)return false;
    wchar_t name[32768]{};DWORD length=32768;const bool ok=QueryFullProcessImageNameW(process,0,name,&length)!=FALSE;CloseHandle(process);if(!ok)return false;
    const auto prefix=directory.wstring()+L"\\";
    // Native process paths and FindSteamVR may use different separator spellings.
    std::wstring path=name,root=prefix;for(auto& c:path)if(c==L'/')c=L'\\';for(auto& c:root)if(c==L'/')c=L'\\';
    return path.size()>root.size()&&_wcsnicmp(path.c_str(),root.c_str(),root.size())==0;
}
}
