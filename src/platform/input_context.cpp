#include "platform/input_context.h"
#include <algorithm>
#include <stdexcept>

namespace slvr {
namespace {
std::vector<wchar_t> EnvironmentBlock(std::vector<std::wstring> variables) {
    std::sort(variables.begin(),variables.end(),[](const auto& a,const auto& b){return _wcsicmp(a.c_str(),b.c_str())<0;});
    std::vector<wchar_t> output;
    for(const auto& value:variables){output.insert(output.end(),value.begin(),value.end());output.push_back(0);}
    if(output.empty())output.push_back(0);
    output.push_back(0);return output;
}
}
std::vector<wchar_t> StandaloneSteamEnvironment(std::vector<std::wstring> variables) {
    variables.erase(std::remove_if(variables.begin(),variables.end(),[](const auto& v){
        return _wcsnicmp(v.c_str(),L"SteamAppId=",11)==0 ||
            _wcsnicmp(v.c_str(),L"SteamGameId=",12)==0 ||
            _wcsnicmp(v.c_str(),L"SteamOverlayGameId=",19)==0;
    }),variables.end());
    return EnvironmentBlock(std::move(variables));
}
std::vector<wchar_t> HardwareInputEnvironment(std::vector<std::wstring> variables) {
    variables.erase(std::remove_if(variables.begin(),variables.end(),[](const auto& value){
        return _wcsnicmp(value.c_str(),L"SteamAppId=",11)==0;
    }),variables.end());
    variables.push_back(L"SteamAppId=0");
    return EnvironmentBlock(std::move(variables));
}
std::optional<int> BootstrapInputContext() {
    wchar_t app[64]{};
    const DWORD length=GetEnvironmentVariableW(L"SteamAppId",app,64);
    if(!length || length>=64)return std::nullopt;
    wchar_t* end=nullptr;
    const auto appId=wcstoull(app,&end,10);
    if(*end || appId<0x80000000ULL || appId>0xffffffffULL)return std::nullopt;
    // Only generated non-Steam IDs need isolation. Never change a real game's context.
    wchar_t* original=GetEnvironmentStringsW();
    if(!original)throw std::runtime_error("Cannot read process environment");
    std::vector<std::wstring> variables;
    for(const wchar_t* entry=original;*entry;entry+=wcslen(entry)+1)variables.emplace_back(entry);
    FreeEnvironmentStringsW(original);
    auto environment=HardwareInputEnvironment(std::move(variables));
    wchar_t executable[32768]{};
    if(!GetModuleFileNameW(nullptr,executable,32768))throw std::runtime_error("Cannot resolve application path");
    std::wstring command=GetCommandLineW();
    HANDLE job=CreateJobObjectW(nullptr,nullptr);
    if(!job)throw std::runtime_error("Cannot create application lifetime job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))) {
        CloseHandle(job);throw std::runtime_error("Cannot configure application lifetime job");
    }
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if(!CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_UNICODE_ENVIRONMENT|CREATE_SUSPENDED,
        environment.data(),nullptr,&startup,&process)) {
        CloseHandle(job);throw std::runtime_error("Cannot launch input-context process");
    }
    if(!AssignProcessToJobObject(job,process.hProcess) || ResumeThread(process.hThread)==static_cast<DWORD>(-1)) {
        TerminateProcess(process.hProcess,1);
        CloseHandle(process.hThread);CloseHandle(process.hProcess);CloseHandle(job);
        throw std::runtime_error("Cannot attach application to lifetime job");
    }
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess,INFINITE);
    DWORD code=1;GetExitCodeProcess(process.hProcess,&code);
    CloseHandle(process.hProcess);CloseHandle(job);
    return static_cast<int>(code);
}
}
