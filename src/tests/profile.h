#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
struct IsolatedProfile {
    std::wstring previous;
    explicit IsolatedProfile(const wchar_t* name){
        wchar_t text[32768]{};GetEnvironmentVariableW(L"LOCALAPPDATA",text,32768);previous=text;
        const auto directory=std::filesystem::current_path()/L"test-profiles"/name;
        std::filesystem::create_directories(directory);SetEnvironmentVariableW(L"LOCALAPPDATA",directory.c_str());
    }
    ~IsolatedProfile(){SetEnvironmentVariableW(L"LOCALAPPDATA",previous.empty()?nullptr:previous.c_str());}
};
