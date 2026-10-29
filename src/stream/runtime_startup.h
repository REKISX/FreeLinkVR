#pragma once
#include "platform/steam_input.h"
#include "platform/setup.h"
#include "common/ipc.h"
#include "platform/window_focus.h"
#include "platform/session_recovery.h"
#include "platform/settings.h"

namespace slvr {
class RuntimeStartup {
    SteamInput input_;
    bool inputOpen_=false,started_=false,ready_=false,focused_=false;
    uint64_t nextCheck_=0,startedAt_=0;
    HANDLE startup_=nullptr;
    PeerSession peer_;
    bool reconnect_=false;
    std::wstring status_=L"Waiting for Steam Link";
public:
    ~RuntimeStartup(){if(startup_)CloseHandle(startup_);}
    const std::wstring& Status() const{return status_;}
    bool TakeReconnect(){const bool value=reconnect_;reconnect_=false;return value;}
    void Tick(HWND window,const SharedState& state,bool allowFocus=true){
        const auto now=GetTickCount64();if(now<nextCheck_)return;nextCheck_=now+1000;
        try{
            if(!inputOpen_){const auto root=FindSteamVR();if(root.empty())throw std::runtime_error("Run Setup in FreeLinkVR.");input_.Open(root/L"tools/steamvr_environments/game/bin/win64/steam_api64.dll");inputOpen_=true;}
            const auto devices=input_.Poll();std::vector<uint64_t> mobile;for(const auto& device:devices)if(device.mobile())mobile.push_back(device.handle);
            const auto sessions=input_.RemoteSessions();const bool wasConnected=peer_.Connected();peer_.Observe(sessions,mobile);
            const bool returned=peer_.TakeReconnect();
            if(returned){reconnect_=true;focused_=false;ready_=false;}
            if(wasConnected!=peer_.Connected()||returned){
                try{const auto root=SettingsPath().parent_path();std::filesystem::create_directories(root);std::ofstream(root/L"session.log",std::ios::app)<<now<<" connected="<<peer_.Connected()<<" reconnect="<<returned<<" remoteSessions="<<sessions.size()<<" mobile="<<mobile.size()<<'\n';}catch(...){}
            }
            if(!peer_.Connected()){status_=L"Waiting for Steam Link to reconnect";return;}
            if(started_&&!state.driverPid&&ready_){started_=ready_=focused_=false;if(startup_){CloseHandle(startup_);startup_=nullptr;}}
            if(started_){
                if(state.driverPid&&state.frameHandle){
                    status_=returned?L"Steam Link reconnected":L"SteamVR ready";
                    if(allowFocus&&!focused_&&now-startedAt_>=5000){focused_=FocusStreamingWindow(window);ready_=focused_;}
                }else if(now-startedAt_>60000)status_=L"SteamVR did not start — check its window";
                return;
            }
            if(state.driverPid){started_=true;startedAt_=now>=5000?now-5000:0;return;}
            // Keep observing the phone after startup. Closing this connection
            // here made every later disconnect/reconnect invisible to us.
            startup_=StartSteamVR();started_=true;startedAt_=now;status_=L"Phone connected — starting SteamVR";
        }catch(const std::exception& e){input_.Close();inputOpen_=false;status_=Wide(e.what());}
    }
};
}
