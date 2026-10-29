#pragma once
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <string>

namespace slvr {
inline std::filesystem::path SettingsPath() {
    wchar_t directory[32768]{};
    if(!GetEnvironmentVariableW(L"LOCALAPPDATA",directory,32768))throw std::runtime_error("LOCALAPPDATA unavailable");
    return std::filesystem::path(directory)/L"FreeLinkVR/settings.ini";
}
struct UserSettings {
    int width=1280,height=720,fps=60,ipdMm=64;
    float lensStrength=0.20f,sensitivity=1.0f,monitorScale=0.85f;
    bool lenses=true,fullscreen=true,monitorPreview=true;
    void Validate() {
        if(!((width==1280 && height==720)||(width==1920 && height==1080))){width=1280;height=720;}
        if(fps!=30 && fps!=60)fps=60;
        ipdMm=std::clamp(ipdMm,55,75);
        lensStrength=std::isfinite(lensStrength)?std::clamp(lensStrength,0.0f,0.6f):0.20f;
        sensitivity=std::isfinite(sensitivity)?std::clamp(sensitivity,0.25f,2.0f):1.0f;
        monitorScale=std::isfinite(monitorScale)?std::clamp(monitorScale,0.5f,1.0f):0.85f;
    }
    static UserSettings Load() {
        UserSettings s;const auto file=SettingsPath();
        auto integer=[&](const wchar_t* key,int value){return static_cast<int>(GetPrivateProfileIntW(L"FreeLinkVR",key,value,file.c_str()));};
        auto real=[&](const wchar_t* key,float value){
            wchar_t text[80]{};const auto fallback=std::to_wstring(value);
            GetPrivateProfileStringW(L"FreeLinkVR",key,fallback.c_str(),text,80,file.c_str());
            wchar_t* end=nullptr;float result=wcstof(text,&end);return end!=text && !*end?result:value;
        };
        s.width=integer(L"width",s.width);s.height=integer(L"height",s.height);
        s.fps=integer(L"fps",s.fps);s.ipdMm=integer(L"ipdMm",s.ipdMm);
        s.lensStrength=real(L"lensStrength",s.lensStrength);s.sensitivity=real(L"sensitivity",s.sensitivity);
        s.monitorScale=real(L"monitorScale",s.monitorScale);
        s.lenses=integer(L"lenses",1)!=0;s.fullscreen=integer(L"fullscreen",1)!=0;s.monitorPreview=integer(L"monitorPreview",1)!=0;s.Validate();return s;
    }
    void Save() const {
        auto s=*this;s.Validate();const auto path=SettingsPath();std::filesystem::create_directories(path.parent_path());
        auto temporary=path;temporary+=L".tmp-"+std::to_wstring(GetCurrentProcessId());
        std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
        file<<"[FreeLinkVR]\nwidth="<<s.width<<"\nheight="<<s.height<<"\nfps="<<s.fps
            <<"\nipdMm="<<s.ipdMm<<"\nlensStrength="<<s.lensStrength<<"\nsensitivity="<<s.sensitivity
            <<"\nlenses="<<s.lenses<<"\nfullscreen="<<s.fullscreen<<"\nmonitorPreview="<<s.monitorPreview<<"\nmonitorScale="<<s.monitorScale<<'\n';
        file.close();if(!file)throw std::runtime_error("Cannot save settings");
        if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace settings file");
        WritePrivateProfileStringW(nullptr,nullptr,nullptr,path.c_str());
    }
};
}
