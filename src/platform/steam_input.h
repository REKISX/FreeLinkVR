#pragma once
#include <windows.h>
#include <array>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#include "common/tracking.h"

namespace slvr {
// Public Steam Input 001 flat ABI, matching the redistributable supplied with
// SteamVR Home. Declarations are local; no Steamworks SDK or binaries are bundled.
struct SteamMotion {
    float qx,qy,qz,qw;
    float accelX,accelY,accelZ;
    float gyroX,gyroY,gyroZ;
};
static_assert(sizeof(SteamMotion)==40);
inline bool FiniteMotion(const SteamMotion& motion) {
    const float values[]={motion.qx,motion.qy,motion.qz,motion.qw,motion.accelX,
        motion.accelY,motion.accelZ,motion.gyroX,motion.gyroY,motion.gyroZ};
    for(float value:values)if(!std::isfinite(value))return false;
    return true;
}
inline bool HasSensorEvidence(const SteamMotion& m) {
    if(!FiniteMotion(m))return false;
    // An identity quaternion with zero acceleration/gyro may be a placeholder.
    const float norm=m.qx*m.qx+m.qy*m.qy+m.qz*m.qz+m.qw*m.qw;
    const bool rotatedQuaternion=norm>0.25f && norm<2.25f &&
        (std::abs(m.qx)+std::abs(m.qy)+std::abs(m.qz)+std::abs(std::abs(m.qw)-1)>1e-4f);
    return rotatedQuaternion ||
        std::abs(m.accelX)+std::abs(m.accelY)+std::abs(m.accelZ)+
        std::abs(m.gyroX)+std::abs(m.gyroY)+std::abs(m.gyroZ)>1e-4f;
}
struct SteamDevice {
    uint64_t handle=0;
    int type=0;
    SteamMotion motion{};
    bool mobile() const {return type==11;} // k_ESteamInputType_MobileTouch
};
inline std::filesystem::path SteamRoot() {
    wchar_t path[32768]{};DWORD size=sizeof(path);
    if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamPath",RRF_RT_REG_SZ,nullptr,path,&size)==ERROR_SUCCESS &&
       std::filesystem::exists(std::filesystem::path(path)/L"steamclient64.dll"))return path;
    if(GetEnvironmentVariableW(L"ProgramFiles(x86)",path,32768)) {
        auto root=std::filesystem::path(path)/L"Steam";
        if(std::filesystem::exists(root/L"steamclient64.dll"))return root;
    }
    throw std::runtime_error("Steam installation not found");
}
class SteamInput {
    HMODULE clientModule_=nullptr,apiModule_=nullptr;
    void* client_=nullptr;void* input_=nullptr;
    int pipe_=0,user_=0;
    bool started_=false;
    using CreatePipe=int(__cdecl*)(void*);
    using Connect=int(__cdecl*)(void*,int);
    using ReleaseUser=void(__cdecl*)(void*,int,int);
    using ReleasePipe=bool(__cdecl*)(void*,int);
    using GetInput=void*(__cdecl*)(void*,int,int,const char*);
    using Init=bool(__cdecl*)(void*);
    using Shutdown=bool(__cdecl*)(void*);
    using RunFrame=void(__cdecl*)(void*);
    using GetControllers=int(__cdecl*)(void*,uint64_t*);
    using GetType=int(__cdecl*)(void*,uint64_t);
    using GetMotion=SteamMotion(__cdecl*)(void*,uint64_t);
    ReleaseUser releaseUser_=nullptr;ReleasePipe releasePipe_=nullptr;
    Shutdown shutdown_=nullptr;RunFrame runFrame_=nullptr;
    GetControllers controllers_=nullptr;GetType type_=nullptr;GetMotion motion_=nullptr;
    void* remote_=nullptr;
    using SessionCount=uint32_t(__cdecl*)(void*);
    using SessionId=uint32_t(__cdecl*)(void*,int);
    SessionCount sessionCount_=nullptr;SessionId sessionId_=nullptr;
    template<class T>T Function(const char* name) {
        const auto function=GetProcAddress(apiModule_,name);
        if(!function)throw std::runtime_error(std::string("Steam API function missing: ")+name);
        return reinterpret_cast<T>(function);
    }
public:
    SteamInput()=default;
    SteamInput(const SteamInput&)=delete;
    SteamInput& operator=(const SteamInput&)=delete;
    ~SteamInput(){Close();}
    void Close() {
        if(started_ && shutdown_)shutdown_(input_);
        if(user_ && releaseUser_)releaseUser_(client_,pipe_,user_);
        if(pipe_ && releasePipe_)releasePipe_(client_,pipe_);
        input_=client_=remote_=nullptr;sessionCount_=nullptr;sessionId_=nullptr;pipe_=user_=0;started_=false;
        if(apiModule_)FreeLibrary(apiModule_);
        if(clientModule_)FreeLibrary(clientModule_);
        apiModule_=clientModule_=nullptr;
    }
    void Open(const std::filesystem::path& explicitApi={}) {
        Close();
        try {
            const auto root=SteamRoot();
            const auto api=explicitApi.empty()
                ? root/L"steamapps/common/SteamVR/tools/steamvr_environments/game/bin/win64/steam_api64.dll" : explicitApi;
            if(!api.is_absolute() || !std::filesystem::is_regular_file(api))
                throw std::runtime_error("SteamVR Home steam_api64.dll not found; pass --steam-api with its absolute path");
            apiModule_=LoadLibraryExW(api.c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
            clientModule_=LoadLibraryExW((root/L"steamclient64.dll").c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
            if(!apiModule_ || !clientModule_)throw std::runtime_error("Cannot load installed Steam libraries");
            // Only use the verified 001 flat ABI. A newer wrapper must not silently
            // dispatch through a differently laid-out ISteamInput vtable.
            for(int version=1;version<=9;++version) {
                const std::string accessor="SteamAPI_SteamInput_v00"+std::to_string(version);
                if(GetProcAddress(apiModule_,accessor.c_str()))
                    throw std::runtime_error("Unsupported Steam API wrapper version; use the verified SteamVR Home copy");
            }
            auto create=Function<CreatePipe>("SteamAPI_ISteamClient_CreateSteamPipe");
            auto connect=Function<Connect>("SteamAPI_ISteamClient_ConnectToGlobalUser");
            releaseUser_=Function<ReleaseUser>("SteamAPI_ISteamClient_ReleaseUser");
            releasePipe_=Function<ReleasePipe>("SteamAPI_ISteamClient_BReleaseSteamPipe");
            auto getInput=Function<GetInput>("SteamAPI_ISteamClient_GetISteamInput");
            auto init=Function<Init>("SteamAPI_ISteamInput_Init");
            shutdown_=Function<Shutdown>("SteamAPI_ISteamInput_Shutdown");
            runFrame_=Function<RunFrame>("SteamAPI_ISteamInput_RunFrame");
            controllers_=Function<GetControllers>("SteamAPI_ISteamInput_GetConnectedControllers");
            type_=Function<GetType>("SteamAPI_ISteamInput_GetInputTypeForHandle");
            motion_=Function<GetMotion>("SteamAPI_ISteamInput_GetMotionData");
            using Factory=void*(__cdecl*)(const char*,int*);
            auto factory=reinterpret_cast<Factory>(GetProcAddress(clientModule_,"CreateInterface"));
            if(!factory)throw std::runtime_error("Steam client factory missing");
            int error=0;client_=factory("SteamClient020",&error);
            if(!client_)throw std::runtime_error("SteamClient020 unavailable");
            pipe_=create(client_);
            if(!pipe_)throw std::runtime_error("Cannot create Steam connection");
            user_=connect(client_,pipe_);
            if(!user_)throw std::runtime_error("Steam has no connected user; open Steam and sign in");
            input_=getInput(client_,user_,pipe_,"SteamInput001");
            if(!input_ || !init(input_))throw std::runtime_error("SteamInput001 initialization failed");
            started_=true;
            // Public Remote Play 001 ABI shipped by this same installed wrapper.
            auto getRemote=reinterpret_cast<GetInput>(GetProcAddress(apiModule_,"SteamAPI_ISteamClient_GetISteamRemotePlay"));
            sessionCount_=reinterpret_cast<SessionCount>(GetProcAddress(apiModule_,"SteamAPI_ISteamRemotePlay_GetSessionCount"));
            sessionId_=reinterpret_cast<SessionId>(GetProcAddress(apiModule_,"SteamAPI_ISteamRemotePlay_GetSessionID"));
            if(getRemote&&sessionCount_&&sessionId_)remote_=getRemote(client_,user_,pipe_,"STEAMREMOTEPLAY_INTERFACE_VERSION001");
        }catch(...){Close();throw;}
    }
    std::vector<uint32_t> RemoteSessions(){
        if(!remote_||!sessionCount_||!sessionId_)return {};
        const uint32_t count=sessionCount_(remote_);if(count>64)throw std::runtime_error("Invalid Remote Play session count");
        std::vector<uint32_t> result;for(uint32_t i=0;i<count;++i){const auto id=sessionId_(remote_,static_cast<int>(i));if(id)result.push_back(id);}return result;
    }
    std::vector<SteamDevice> Poll() {
        if(!started_)return {};
        runFrame_(input_);
        std::array<uint64_t,64> handles{};
        const int count=controllers_(input_,handles.data());
        if(count<0 || count>static_cast<int>(handles.size()))throw std::runtime_error("Invalid Steam controller count");
        std::vector<SteamDevice> devices;
        for(int i=0;i<count;++i) {
            const auto handle=handles[static_cast<size_t>(i)];
            if(handle) {
                const int type=type_(input_,handle);
                devices.push_back({handle,type,type==11 ? motion_(input_,handle) : SteamMotion{}});
            }
        }
        return devices;
    }
};
}
