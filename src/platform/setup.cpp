#include "platform/setup.h"
#include "common/keyvalues.h"
#include "platform/input_context.h"
#include <tlhelp32.h>
#include <wbemidl.h>
#include <wrl/client.h>
#include <shellapi.h>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <map>

namespace slvr {
using Path=std::filesystem::path;
std::wstring Wide(const std::string& text){
    if(text.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
    if(!n)throw std::runtime_error("Invalid UTF-8");std::wstring value(n,0);
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),value.data(),n);return value;
}
std::string Utf8(const std::wstring& text){
    if(text.empty())return {};int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    if(!n)throw std::runtime_error("Invalid Unicode path");std::string value(n,0);
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),value.data(),n,nullptr,nullptr);return value;
}
Path AppDirectory(){wchar_t text[32768]{};const auto n=GetModuleFileNameW(nullptr,text,32768);if(!n||n>=32768)throw std::runtime_error("Cannot locate application");return Path(text).parent_path();}
static std::string Read(const Path& path){
    std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("Cannot read "+Utf8(path.wstring()));
    const auto size=std::filesystem::file_size(path);if(size>16*1024*1024)throw std::runtime_error("Configuration file is too large");
    std::string data(static_cast<size_t>(size),'\0');if(size&&!f.read(data.data(),static_cast<std::streamsize>(size)))throw std::runtime_error("Cannot read configuration");return data;
}
Path FindSteam(){wchar_t text[32768]{};DWORD size=sizeof(text);if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamPath",RRF_RT_REG_SZ,nullptr,text,&size)==ERROR_SUCCESS && std::filesystem::is_regular_file(Path(text)/L"steam.exe"))return text;return {};}
static uint32_t AccountId(){
    DWORD id=0,size=sizeof(id);RegGetValueW(HKEY_CURRENT_USER,L"Software\\Valve\\Steam\\ActiveProcess",L"ActiveUser",RRF_RT_REG_DWORD,nullptr,&id,&size);
    if(id)return id;
    const auto file=FindSteam()/L"config/loginusers.vdf";
    if(std::filesystem::exists(file)){
        const auto root=kv::TextParser(Read(file)).Parse();const auto users=kv::Find(root,"users");
        if(users)for(const auto& user:users->children)if(kv::Value(user.children,"MostRecent")=="1"){
            try{return static_cast<uint32_t>(std::stoull(user.key)&0xffffffffu);}catch(...){}
        }
    }throw std::runtime_error("Sign in to Steam and run Setup again.");
}
static Path UserConfig(){return FindSteam()/L"userdata"/std::to_wstring(AccountId())/L"config";}
static Path ControllerConfig(){return FindSteam()/L"steamapps/common/Steam Controller Configs"/std::to_wstring(AccountId())/L"config";}
static std::vector<Path> SteamLibraries(){
    const auto steam=FindSteam();if(steam.empty())return {};std::vector<Path> roots{steam};
    const auto libraries=steam/L"steamapps/libraryfolders.vdf";
    if(std::filesystem::exists(libraries)){
        const auto parsed=kv::TextParser(Read(libraries)).Parse();const auto list=kv::Find(parsed,"libraryfolders");
        if(list)for(const auto& item:list->children){const auto path=kv::Value(item.children,"path");if(!path.empty())roots.emplace_back(Wide(path));}
    }
    return roots;
}
Path FindSteamVR(){
    for(const auto& root:SteamLibraries()){
        const auto manifest=root/L"steamapps/appmanifest_250820.acf";
        if(!std::filesystem::exists(manifest))continue;
        const auto tree=kv::TextParser(Read(manifest)).Parse();const auto app=kv::Find(tree,"AppState");
        if(!app)continue;
        uint32_t state=0;try{state=static_cast<uint32_t>(std::stoul(kv::Value(app->children,"StateFlags")));}catch(...){continue;}
        if(!(state&4u)||(state&(32u|128u|2048u)))continue;
        const auto directory=kv::Value(app->children,"installdir");
        if(directory.empty() || Path(Wide(directory)).has_parent_path())continue;
        const auto runtime=root/L"steamapps/common"/Wide(directory);
        if(std::filesystem::exists(runtime/L"bin/win64/vrstartup.exe") && std::filesystem::exists(runtime/L"bin/win64/vrpathreg.exe"))return runtime;
    }return {};
}
static bool ProcessRunning(const wchar_t* name){
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot inspect running processes");
    PROCESSENTRY32W item{sizeof(item)};bool found=false;
    if(Process32FirstW(snapshot,&item))do{if(!_wcsicmp(item.szExeFile,name)){found=true;break;}}while(Process32NextW(snapshot,&item));
    CloseHandle(snapshot);return found;
}
bool SteamRunning(){return ProcessRunning(L"steam.exe");}
bool SteamWindowMode(){
    using Microsoft::WRL::ComPtr;const HRESULT initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    struct Cleanup{bool active;~Cleanup(){if(active)CoUninitialize();}}cleanup{SUCCEEDED(initialized)};
    ComPtr<IWbemLocator> locator;ComPtr<IWbemServices> service;ComPtr<IEnumWbemClassObject> result;
    if(FAILED(CoCreateInstance(CLSID_WbemLocator,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&locator))))return false;
    BSTR ns=SysAllocString(L"ROOT\\CIMV2");HRESULT hr=locator->ConnectServer(ns,nullptr,nullptr,nullptr,0,nullptr,nullptr,&service);SysFreeString(ns);if(FAILED(hr))return false;
    if(FAILED(CoSetProxyBlanket(service.Get(),RPC_C_AUTHN_WINNT,RPC_C_AUTHZ_NONE,nullptr,RPC_C_AUTHN_LEVEL_CALL,RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE)))return false;
    BSTR language=SysAllocString(L"WQL"),query=SysAllocString(L"SELECT CommandLine FROM Win32_Process WHERE Name='steam.exe'");
    hr=service->ExecQuery(language,query,WBEM_FLAG_FORWARD_ONLY|WBEM_FLAG_RETURN_IMMEDIATELY,nullptr,&result);SysFreeString(language);SysFreeString(query);if(FAILED(hr))return false;
    while(true){ComPtr<IWbemClassObject> object;ULONG count=0;if(FAILED(result->Next(2000,1,&object,&count))||!count)break;VARIANT value;VariantInit(&value);
        const HRESULT read=object->Get(L"CommandLine",0,&value,nullptr,nullptr);bool enabled=false;
        if(SUCCEEDED(read)&&value.vt==VT_BSTR&&value.bstrVal){int argc=0;auto argv=CommandLineToArgvW(value.bstrVal,&argc);if(argv){for(int i=1;i<argc;++i)if(!_wcsicmp(argv[i],L"-vrdisable"))enabled=true;LocalFree(argv);}}
        VariantClear(&value);if(enabled)return true;
    }return false;
}
std::vector<wchar_t> CleanSteamEnvironment(){
    wchar_t* block=GetEnvironmentStringsW();if(!block)throw std::runtime_error("Cannot read environment");std::vector<std::wstring> vars;
    for(const wchar_t* p=block;*p;p+=wcslen(p)+1)vars.emplace_back(p);FreeEnvironmentStringsW(block);return StandaloneSteamEnvironment(std::move(vars));
}
static std::wstring Arg(const std::wstring& text){
    std::wstring result=L"\"";size_t slashes=0;for(wchar_t c:text){if(c==L'\\'){++slashes;continue;}if(c==L'"'){result.append(slashes*2+1,L'\\');result+=c;}else{result.append(slashes,L'\\');result+=c;}slashes=0;}result.append(slashes*2,L'\\');return result+L'"';
}
static HANDLE Launch(const Path& executable,const std::vector<std::wstring>& arguments){
    std::wstring command=Arg(executable.wstring());for(const auto& a:arguments)command+=L" "+Arg(a);
    auto environment=CleanSteamEnvironment();STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
    startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
    const auto working=executable.parent_path();
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_UNICODE_ENVIRONMENT|CREATE_NO_WINDOW,environment.data(),working.c_str(),&startup,&process))throw std::runtime_error("Cannot start "+Utf8(executable.filename().wstring())+" (Windows "+std::to_string(GetLastError())+")");
    CloseHandle(process.hThread);return process.hProcess;
}
static void RunTool(const Path& executable,const std::vector<std::wstring>& arguments){
    HANDLE process=Launch(executable,arguments);const DWORD wait=WaitForSingleObject(process,15000);
    if(wait!=WAIT_OBJECT_0){TerminateProcess(process,1);CloseHandle(process);throw std::runtime_error("SteamVR registration tool timed out");}
    DWORD code=1;GetExitCodeProcess(process,&code);CloseHandle(process);if(code)throw std::runtime_error("SteamVR registration failed: "+std::to_string(code));
}
static void StopSteam(){
    if(!SteamRunning())return;HANDLE request=Launch(FindSteam()/L"steam.exe",{L"-shutdown"});CloseHandle(request);
    for(int i=0;i<300 && SteamRunning();++i)std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if(SteamRunning())throw std::runtime_error("Close your games and Steam, then try again. No processes were forcibly terminated.");
}
static void EnsureSteamWindowMode(){
    const auto steam=FindSteam();if(steam.empty())throw std::runtime_error("Install Steam and sign in.");
    if(SteamRunning()&&!SteamWindowMode())StopSteam();
    if(!SteamRunning()){HANDLE process=Launch(steam/L"steam.exe",{L"-vrdisable"});CloseHandle(process);
        const auto deadline=GetTickCount64()+30000;
        while(GetTickCount64()<deadline&&!SteamWindowMode())std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if(!SteamWindowMode())throw std::runtime_error("Could not start Steam in window-streaming mode.");
    }
}
uint64_t GameId(const Path& exe){const auto quoted="\""+Utf8(std::filesystem::absolute(exe).wstring())+"\"";return (static_cast<uint64_t>(kv::ShortcutId(quoted,"FreeLinkVR"))<<32)|0x02000000ULL;}
static bool DriverRegistered(const Path& package){
    wchar_t appData[32768]{};if(!GetEnvironmentVariableW(L"LOCALAPPDATA",appData,32768))return false;
    const auto file=Path(appData)/L"openvr/openvrpaths.vrpath";if(!std::filesystem::exists(file))return false;
    const auto json=Read(file);const auto key=json.find("\"external_drivers\"");if(key==std::string::npos)return false;
    const auto begin=json.find('[',key),end=json.find(']',begin);if(begin==std::string::npos||end==std::string::npos)return false;
    auto needle=kv::Quote(Utf8(std::filesystem::absolute(package/L"freelinkvr").wstring()));auto list=json.substr(begin,end-begin);
    std::transform(needle.begin(),needle.end(),needle.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});std::transform(list.begin(),list.end(),list.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return list.find(needle)!=std::string::npos;
}
static bool HasShortcut(const Path& package){
    const auto path=UserConfig()/L"shortcuts.vdf";if(!std::filesystem::exists(path))return false;
    const auto root=kv::BinaryParser(Read(path)).Parse();const auto shortcuts=kv::Find(root,"shortcuts");if(!shortcuts)return false;
    const auto expected="\""+Utf8((package/L"FreeLinkVR-Stream.exe").wstring())+"\"";
    for(const auto& n:shortcuts->children)if(kv::Value(n.children,"AppName")=="FreeLinkVR" && kv::Value(n.children,"Exe")==expected)return true;return false;
}
bool IsSetupComplete(const Path& package){
    try{return !FindSteamVR().empty() && DriverRegistered(package) && HasShortcut(package) && std::filesystem::exists(ControllerConfig()/L"freelinkvr/controller_mobile_touch.vdf");}catch(...){return false;}
}
static void AtomicWrite(const Path& path,const std::string& data){
    std::filesystem::create_directories(path.parent_path());auto temporary=path;temporary+=L".freelinkvr-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".tmp";
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create configuration temporary file");
    DWORD written=0;const bool ok=WriteFile(file,data.data(),static_cast<DWORD>(data.size()),&written,nullptr)&&written==data.size()&&FlushFileBuffers(file);CloseHandle(file);
    if(!ok||!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(temporary.c_str());throw std::runtime_error("Cannot replace configuration file");}
}
class FileTransaction {
    struct Saved{Path path;std::string data;bool existed;};std::vector<Saved> saved_;bool accepted_=false;
public:
    ~FileTransaction(){if(!accepted_)Rollback();}
    bool Rollback() noexcept{
        bool ok=true;for(auto it=saved_.rbegin();it!=saved_.rend();++it)try{if(it->existed)AtomicWrite(it->path,it->data);else std::filesystem::remove(it->path);}catch(...){ok=false;}
        accepted_=true;return ok;
    }
    void Write(const Path& path,const std::string& data){
        Saved before{path,{},std::filesystem::exists(path)};if(before.existed){before.data=Read(path);auto backup=path;backup+=L".freelinkvr-backup-"+std::to_wstring(GetTickCount64());std::filesystem::copy_file(path,backup);}
        saved_.push_back(std::move(before));AtomicWrite(path,data);
    }
    void Accept(){accepted_=true;}
};
static std::string ShortcutFile(const Path& package,const Path& file){
    auto root=std::filesystem::exists(file)?kv::BinaryParser(Read(file)).Parse():std::vector<kv::Node>{kv::Object("shortcuts")};
    auto list=kv::Find(root,"shortcuts");if(!list||list->type!=0)throw std::runtime_error("Invalid shortcuts root; original file preserved");
    kv::Node* target=nullptr;unsigned next=0;
    for(auto& entry:list->children){
        try{next=std::max(next,static_cast<unsigned>(std::stoul(entry.key))+1);}catch(...){throw std::runtime_error("Invalid shortcut index");}
        const auto name=kv::Value(entry.children,"AppName"),exe=kv::Value(entry.children,"Exe");
        if(name=="FreeLinkVR" || (name=="SteamLinkVR"&&exe.find("SteamLinkVR.exe")!=std::string::npos))target=&entry;
    }
    if(!target){list->children.push_back(kv::Object(std::to_string(next)));target=&list->children.back();}
    auto& f=target->children;const auto exe=package/L"FreeLinkVR-Stream.exe";
    kv::Put(f,kv::Integer("appid",static_cast<uint32_t>(GameId(exe)>>32)));
    kv::Put(f,kv::Text("AppName","FreeLinkVR"));kv::Put(f,kv::Text("Exe","\""+Utf8(exe.wstring())+"\""));kv::Put(f,kv::Text("StartDir","\""+Utf8(package.wstring())+"\""));
    kv::Put(f,kv::Text("LaunchOptions",""));for(const char* key:{"icon","ShortcutPath","DevkitGameID","FlatpakAppID"})if(!kv::Find(f,key))kv::Put(f,kv::Text(key,""));
    for(const char* key:{"IsHidden","OpenVR","Devkit","DevkitOverrideAppID"})kv::Put(f,kv::Integer(key,0));
    kv::Put(f,kv::Integer("AllowDesktopConfig",1));kv::Put(f,kv::Integer("AllowOverlay",1));if(!kv::Find(f,"tags"))kv::Put(f,kv::Object("tags"));
    auto encoded=kv::EncodeBinary(root);if(kv::EncodeBinary(kv::BinaryParser(encoded).Parse())!=encoded)throw std::runtime_error("Shortcut roundtrip failed");return encoded;
}
SetupResult Install(const Path& package){
    try{
        if(FindSteam().empty())return {false,false,L"Install Steam and sign in, then choose Setup."};
        const auto runtime=FindSteamVR();
        if(runtime.empty()){EnsureSteamWindowMode();HANDLE process=Launch(FindSteam()/L"steam.exe",{L"steam://install/250820"});CloseHandle(process);return {false,true,L"Confirm the SteamVR installation in Steam. Setup will continue after the download."};}
        if(!std::filesystem::exists(package/L"FreeLinkVR-Stream.exe")||!std::filesystem::exists(package/L"freelinkvr/bin/win64/driver_freelinkvr.dll"))throw std::runtime_error("FreeLinkVR files are missing. Extract the entire package into one folder.");
        const auto user=UserConfig(),controllers=ControllerConfig();
        if(ProcessRunning(L"FreeLinkVR-Stream.exe"))throw std::runtime_error("Close the FreeLinkVR stream before running Setup.");
        StopSteam();if(ProcessRunning(L"vrserver.exe")||ProcessRunning(L"vrcompositor.exe"))throw std::runtime_error("Close SteamVR and run Setup again.");
        const auto selectionFile=controllers/L"configset_controller_mobile_touch.vdf";
        auto selection=std::filesystem::exists(selectionFile)?kv::TextParser(Read(selectionFile)).Parse():std::vector<kv::Node>{kv::Object("controller_config")};
        auto configs=kv::Find(selection,"controller_config");if(!configs||configs->type!=0)throw std::runtime_error("Invalid controller selection; original file preserved");
        auto selected=kv::Object("freelinkvr");selected.children.push_back(kv::Text("autosave","1"));kv::Put(configs->children,std::move(selected));
        const auto profile=Read(package/L"steaminput/controller_mobile_touch.vdf");kv::TextParser(profile).Parse();
        FileTransaction transaction;
        if(SteamRunning())throw std::runtime_error("Steam started during setup. Close it and run Setup again.");
        try{
            transaction.Write(user/L"shortcuts.vdf",ShortcutFile(package,user/L"shortcuts.vdf"));
            transaction.Write(controllers/L"freelinkvr/controller_mobile_touch.vdf",profile);
            transaction.Write(selectionFile,kv::EncodeText(selection));
            RunTool(runtime/L"bin/win64/vrpathreg.exe",{L"adddriver",std::filesystem::absolute(package/L"freelinkvr").wstring()});
            transaction.Accept();
        }catch(...){if(!transaction.Rollback())throw std::runtime_error("Some files could not be restored. The .freelinkvr-backup files remain next to the Steam configuration files.");throw;}
        EnsureSteamWindowMode();
        const auto validationDeadline=GetTickCount64()+15000;
        while(GetTickCount64()<validationDeadline&&!IsSetupComplete(package))std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if(!IsSetupComplete(package))throw std::runtime_error("Files were installed, but final validation failed. Sign in to Steam and run Setup again.");
        return {true,false,L"Ready. Connect your phone with Steam Link and choose Start."};
    }catch(const std::exception& e){return {false,false,Wide(e.what())};}
}
void StartSession(const Path& package){
    if(!IsSetupComplete(package))throw std::runtime_error("Run Setup first.");EnsureSteamWindowMode();
    const auto uri=L"steam://rungameid/"+std::to_wstring(GameId(package/L"FreeLinkVR-Stream.exe"));HANDLE process=Launch(FindSteam()/L"steam.exe",{uri});CloseHandle(process);
}
HANDLE StartSteamVR(){const auto runtime=FindSteamVR();if(runtime.empty())throw std::runtime_error("SteamVR is not installed. Run Setup.");if(ProcessRunning(L"vrserver.exe"))throw std::runtime_error("SteamVR is already running without FreeLinkVR. Close it; startup will retry automatically.");return Launch(runtime/L"bin/win64/vrstartup.exe",{});}
std::vector<SteamGame> SteamGames(){
    std::map<uint64_t,std::wstring> games;
    for(const auto& library:SteamLibraries()){
        std::error_code ec;const auto apps=library/L"steamapps";
        for(const auto& entry:std::filesystem::directory_iterator(apps,std::filesystem::directory_options::skip_permission_denied,ec)){
            if(entry.path().extension()!=L".acf"||entry.path().filename().wstring().find(L"appmanifest_")!=0)continue;
            try{
                const auto tree=kv::TextParser(Read(entry.path())).Parse();const auto app=kv::Find(tree,"AppState");if(!app)continue;
                const auto id=std::stoull(kv::Value(app->children,"appid"));const auto flags=std::stoul(kv::Value(app->children,"StateFlags"));
                if(!id||id>=0x80000000ULL||id==250820||id==413080||id==228980||!(flags&4)||flags&(32|128|2048))continue;
                const auto name=kv::Value(app->children,"name");if(!name.empty())games[id]=Wide(name);
            }catch(...){}
        }
    }
    try{
        const auto file=UserConfig()/L"shortcuts.vdf";
        if(std::filesystem::exists(file)){
            const auto tree=kv::BinaryParser(Read(file)).Parse();const auto shortcuts=kv::Find(tree,"shortcuts");
            if(shortcuts)for(const auto& shortcut:shortcuts->children){
                const auto name=kv::Value(shortcut.children,"AppName");const auto id=kv::Find(shortcut.children,"appid");
                if(!id||id->type!=2||!id->number||name.empty()||name=="FreeLinkVR"||name=="SteamLinkVR")continue;
                games[(static_cast<uint64_t>(id->number)<<32)|0x02000000ULL]=Wide(name);
            }
        }
    }catch(...){}
    std::vector<SteamGame> result;for(const auto& item:games)result.push_back({item.first,item.second});
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return _wcsicmp(a.name.c_str(),b.name.c_str())<0;});return result;
}
void LaunchSteamGame(uint64_t id){
    if(!id)throw std::runtime_error("Invalid Steam game ID");const auto steam=FindSteam();if(steam.empty())throw std::runtime_error("Steam was not found.");
    HANDLE process=Launch(steam/L"steam.exe",{L"steam://rungameid/"+std::to_wstring(id)});CloseHandle(process);
}
}
