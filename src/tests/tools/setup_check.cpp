#include "platform/setup.h"
#include <iostream>
int wmain(int argc,wchar_t** argv){
    try{
        if(argc!=3)throw std::runtime_error("Usage: setup_check --status|--install|--start PACKAGE_DIRECTORY");
        const std::filesystem::path package=std::filesystem::absolute(argv[2]);const std::wstring action=argv[1];
        if(action==L"--install"){
            const auto result=slvr::Install(package);std::cout<<slvr::Utf8(result.message)<<'\n';if(!result.complete)return result.waitingForDownload?2:1;
        }else if(action==L"--start")slvr::StartSession(package);
        else if(action!=L"--status")throw std::runtime_error("Unknown action");
        const bool complete=slvr::IsSetupComplete(package);
        std::cout<<"setupComplete="<<complete<<" steamWindowMode="<<slvr::SteamWindowMode()<<" gameId="<<slvr::GameId(package/L"FreeLinkVR-Stream.exe")<<'\n';
        std::cout<<"SteamVR="<<slvr::Utf8(slvr::FindSteamVR().wstring())<<'\n';return complete?0:3;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
