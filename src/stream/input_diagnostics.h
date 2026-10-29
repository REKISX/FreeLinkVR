#pragma once
#include <windows.h>
#include <filesystem>
namespace slvr {
constexpr UINT RequestInputDiagnostics=WM_APP+20;
int RunInputDiagnostics(HINSTANCE instance,int show,const std::filesystem::path& api,int seconds,bool hidden);
}
