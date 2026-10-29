#include "stream/udp.h"
#include "common/ipc.h"
#include "common/graphics.h"
#include "platform/steam_input.h"
#include "platform/input_context.h"
#include "common/keyvalues.h"
#include "platform/settings.h"
#include "stream/frame_receiver.h"
#include "profile.h"
#include "stream/library_math.h"
#include "stream/library_manifest.h"
#include "platform/session_recovery.h"
#include <iostream>
#include <limits>
#include <vector>

using namespace slvr;
void Require(bool condition,const char* message) {if (!condition) throw std::runtime_error(message);}
bool Near(double a,double b,double tolerance=1e-6) {return std::abs(a-b)<tolerance;}
constexpr const char* TestToken="0123456789abcdef0123456789abcdef";
void TestSessionRecovery(){
    PeerSession peer;
    peer.Observe({},{});Require(!peer.Connected()&&!peer.TakeReconnect(),"No recovery before phone connects");
    peer.Observe({10},{100});Require(peer.Connected()&&!peer.TakeReconnect(),"Initial phone connection uses normal startup");
    peer.Observe({10},{100});Require(!peer.TakeReconnect(),"Stable session does not recreate capture");
    peer.Observe({},{});Require(!peer.Connected()&&!peer.TakeReconnect(),"Leaving phone marks session disconnected");
    peer.Observe({11},{101});Require(peer.TakeReconnect()&&!peer.TakeReconnect(),"Returning phone recovers capture exactly once");
    peer.Observe({12},{101});Require(peer.TakeReconnect(),"Changed Remote Play ID detects reconnect missed between polls");
    peer.Observe({12},{999});Require(!peer.TakeReconnect(),"Controller remapping does not reset a stable Remote Play session");
    peer.Observe({},{});peer.Observe({12},{999});Require(peer.TakeReconnect(),"Same session ID recovers after observed disconnection");
    PeerSession fallback;fallback.Observe({},{55});Require(!fallback.TakeReconnect(),"Mobile controller fallback initial connection");
    fallback.Observe({},{56});Require(fallback.TakeReconnect(),"Mobile controller fallback detects replacement");
    PeerSession ordered;ordered.Observe({2,1},{9});ordered.Observe({1,2},{9});Require(!ordered.TakeReconnect(),"Session enumeration order does not trigger recovery");
    peer.Observe({},{});peer.Observe({13},{});peer.Observe({13},{});Require(peer.TakeReconnect(),"Pending reconnect survives until capture consumes it");
}
void TestLibrary(){
    const auto center=LibraryGaze(.5f,.5f);
    Require(center&&Near(center->x,.5)&&Near(center->y,.5),"Overlay center maps to bitmap center");
    const auto top=LibraryGaze(.25f,1.f);
    Require(top&&Near(top->x,.25)&&Near(top->y,0),"OpenVR lower-left coordinates map to top-down image");
    Require(!LibraryGaze(-.1f,.5f)&&!LibraryGaze(.5f,1.1f),"Out-of-bounds intersection rejected");
    Require(LibraryHit(.5f,150.f/768,8,0)==0,"First game selected");
    Require(LibraryHit(.5f,150.f/768,8,1)==6,"Second page selection");
    Require(LibraryHit(.5f,310.f/768,8,1)==-1,"Empty row cannot launch game");
    Require(LibraryHit(.85f,680.f/768,8,0)==-3&&LibraryHit(.85f,680.f/768,8,1)==-1,"Next page bounds");
    Require(LibraryHit(.5f,680.f/768,8,0)==-1,"Native dashboard owns tab switching; no fake close button");
    Require(LibraryHit(std::numeric_limits<float>::quiet_NaN(),.5f,8,0)==-1,"Nonfinite gaze rejected");
    Require(GameKey(123)=="steam.app.123"&&GameKey((uint64_t(3841404589)<<32)|0x02000000)=="steam.app.3841404589","Steam native and shortcut VR keys");
    const auto manifest=GameManifest({{123,L"Test \u00e9 \"VR\"\n\\"}});
    Require(manifest.find("steam://rungameid/123")!=std::string::npos&&manifest.find("\\\"VR\\\"\\u000a\\\\")!=std::string::npos,"Manifest safely escapes game names and preserves launch ID");
}

PosePacket Packet(uint64_t sequence=1) {
    PosePacket packet{}; std::memcpy(packet.magic,"SLVR",4); packet.version=1;
    std::memcpy(packet.token,TestToken,32); packet.sequence=sequence; packet.w=2;
    return packet;
}
void TestTracking() {
    const auto standalone=StandaloneSteamEnvironment({L"SteamAppId=0",L"SteamGameId=15896433201078337536",L"SteamOverlayGameId=1",L"PATH=C:\\Windows",L"=C:=C:\\"});
    std::vector<std::wstring> clean;for(const wchar_t* p=standalone.data();*p;p+=wcslen(p)+1)clean.emplace_back(p);
    Require(clean.size()==2&&clean[0]==L"=C:=C:\\"&&clean[1]==L"PATH=C:\\Windows","Official SteamVR launcher does not inherit bridge AppIDs; OS environment preserved");
    Require(standalone[standalone.size()-1]==0&&standalone[standalone.size()-2]==0,"Clean environment has two terminators");
    const auto env=HardwareInputEnvironment({L"SteamAppId=3701176774",L"SteamGameId=15896433201078337536",L"PATH=C:\\Windows",L"=C:=C:\\"});
    std::vector<std::wstring> entries;
    for(const wchar_t* p=env.data();*p;p+=wcslen(p)+1)entries.emplace_back(p);
    Require(std::count(entries.begin(),entries.end(),L"SteamAppId=0")==1,"Hardware input context uses AppID zero");
    Require(std::find(entries.begin(),entries.end(),L"SteamAppId=3701176774")==entries.end(),"Generated AppID does not leak into hardware context");
    Require(std::find(entries.begin(),entries.end(),L"SteamGameId=15896433201078337536")!=entries.end(),"Steam shortcut game identity is preserved");
    Require(std::find(entries.begin(),entries.end(),L"=C:=C:\\")!=entries.end() && env[env.size()-1]==0 && env[env.size()-2]==0,"Windows environment and double terminator preserved");
    SteamMotion motion{};
    Require(!HasSensorEvidence(motion),"All-zero Steam motion must not be reported as sensor evidence");
    motion.qw=1;
    Require(!HasSensorEvidence(motion),"Identity-only Steam motion is not proof of IMU data");
    motion.qw=-1;
    Require(!HasSensorEvidence(motion),"Quaternion sign does not create motion evidence");
    motion.accelY=1;
    Require(HasSensorEvidence(motion),"Gravity is nonzero sensor evidence");
    motion.gyroX=std::numeric_limits<float>::quiet_NaN();
    Require(!FiniteMotion(motion) && !HasSensorEvidence(motion),"Reject non-finite Steam motion");
    const std::chrono::steady_clock::time_point epoch{};
    const auto beforeCopy=epoch+std::chrono::milliseconds(15);
    const auto deadline=NextVsync(epoch,beforeCopy,60);
    Require(deadline>beforeCopy && deadline<epoch+std::chrono::milliseconds(17),"Vsync targets the submission interval");
    Require(deadline<epoch+std::chrono::milliseconds(18),"Late GPU completion must not add another refresh interval");
    MouseMotion mouse;mouse.AddRaw(4,-3);mouse.SetLegacy(4,-3);
    Require(mouse.Take()==std::make_pair(4.0,-3.0),"Raw and legacy mouse messages are not double counted");
    Require(mouse.Take()==std::make_pair(0.0,0.0),"Mouse deltas consumed once");
    mouse.SetLegacy(2,3);mouse.SetLegacy(7,9);mouse.SetLegacy(0,0);
    Require(mouse.Take()==std::make_pair(7.0,9.0),"Legacy absolute positions are coalesced, warp does not erase motion");
    mouse.AddLegacyDelta(4,3);mouse.AddLegacyDelta(-1,2);
    Require(mouse.Take()==std::make_pair(3.0,5.0),"Free cursor movement accumulates relative legacy deltas");
    mouse.AddLegacyDelta(4,3);mouse.AddRaw(4,3);
    Require(mouse.Take()==std::make_pair(4.0,3.0),"Free cursor fallback does not double count Raw Input");
    Quaternion zero{0,0,0,0},bad{std::numeric_limits<double>::quiet_NaN(),0,0,0};
    Require(!Normalize(zero) && !Normalize(bad),"Reject bad quaternion");
    auto q=FromYawPitch(Pi/2,0);
    auto forward=Multiply(Multiply(q,{0,0,0,-1}),Inverse(q));
    Require(Near(forward.x,-1) && Near(forward.z,0),"OpenVR yaw convention");
    auto neutral=Multiply(Inverse(q),q);
    Require(Near(neutral.w,1) && Near(neutral.y,0),"Recenter quaternion");
    float u=0,v=0; Distort(0.25f,0.75f,0,0,u,v);
    Require(Near(u,0.25) && Near(v,0.75),"Identity distortion");
    Distort(0.5f,0.5f,0.3f,0.2f,u,v);
    Require(Near(u,0.5) && Near(v,0.5),"Lens center fixed");
    SharedState state{}; state.viewerPid=1; state.poseTimeMs=1000;
    Require(PoseFresh(state,1500) && !PoseFresh(state,1501) && !PoseFresh(state,999),"Tracking timeout boundary");
    state.viewerPid=0; Require(!PoseFresh(state,1000),"Closed viewer is not tracked");
}

void TestConfiguration(){
    const std::string original="\"root\" { \"path\" \"C:\\\\Some Folder\" \"group\" { \"id\" \"1\" } \"group\" { \"id\" \"2\" } }";
    const auto text=kv::TextParser(original).Parse();const auto round=kv::TextParser(kv::EncodeText(text)).Parse();
    Require(kv::EncodeText(text)==kv::EncodeText(round),"Text configuration roundtrip preserves duplicate groups and paths");
    std::vector<kv::Node> roots{kv::Object("shortcuts")};auto first=kv::Object("0");first.children={kv::Text("AppName","Unrelated game"),kv::Text("Exe","\"C:\\Game\\game.exe\""),kv::Integer("appid",123)};roots[0].children.push_back(first);
    const auto binary=kv::EncodeBinary(roots);Require(kv::EncodeBinary(kv::BinaryParser(binary).Parse())==binary,"Binary shortcuts preserve unrelated game bytes");
    bool rejected=false;try{kv::BinaryParser(binary.substr(0,binary.size()-1)).Parse();}catch(...){rejected=true;}Require(rejected,"Truncated shortcuts are rejected before edits");
    Require(kv::ShortcutId("\"C:\\Users\\REKISX\\Documents\\ChatGPT\\123\\SteamLinkVR\\build\\package\\SteamLinkVR.exe\"","SteamLinkVR")==3701176774u,"Shortcut CRC matches Steam-compatible known ID");
    UserSettings settings;settings.width=1;settings.height=1;settings.fps=90;settings.lensStrength=std::numeric_limits<float>::quiet_NaN();settings.sensitivity=9;settings.ipdMm=90;settings.Validate();
    Require(settings.width==1280&&settings.height==720&&settings.fps==60&&Near(settings.lensStrength,0.2f)&&settings.ipdMm==75&&settings.sensitivity==2,"Settings reject invalid resolution, NaN and out-of-range optics");
    settings.monitorScale=std::numeric_limits<float>::quiet_NaN();settings.Validate();Require(Near(settings.monitorScale,.85f),"Nonfinite local monitor scale returns to default");
    settings.monitorScale=4;settings.Validate();Require(settings.monitorScale==1,"Local monitor scale clamps to 100 percent");
    settings.width=1920;settings.height=1080;settings.lensStrength=.35f;settings.monitorPreview=false;settings.monitorScale=.7f;settings.Save();const auto loaded=UserSettings::Load();Require(loaded.width==1920&&loaded.height==1080&&Near(loaded.lensStrength,.35f)&&!loaded.monitorPreview&&Near(loaded.monitorScale,.7f),"Settings and local monitor size persist in an isolated test profile");
}

void TestRetainedFrame(){
    Graphics producer,consumer;producer.Open();consumer.Open(&producer.luid);FrameReceiver receiver(consumer);
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=16;desc.ArraySize=desc.MipLevels=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    ComPtr<ID3D11Texture2D> source;ComPtr<IDXGIKeyedMutex> writer;HANDLE handle=nullptr;
    auto create=[&]{source.Reset();writer.Reset();Check(producer.device->CreateTexture2D(&desc,nullptr,&source),"Create retained-frame producer");Check(source.As(&writer),"Create retained-frame mutex");ComPtr<IDXGIResource> resource;Check(source.As(&resource),"Query retained-frame handle");Check(resource->GetSharedHandle(&handle),"Read retained-frame handle");};
    auto publish=[&](uint32_t color){Require(writer->AcquireSync(0,100)==S_OK,"Acquire test producer");std::vector<uint32_t> pixels(16*16,color);producer.context->UpdateSubresource(source.Get(),0,nullptr,pixels.data(),16*4,0);producer.context->Flush();Check(writer->ReleaseSync(1),"Publish retained frame");};
    auto pixel=[&]{D3D11_TEXTURE2D_DESC stagingDesc{};receiver.Texture()->GetDesc(&stagingDesc);stagingDesc.Usage=D3D11_USAGE_STAGING;stagingDesc.BindFlags=stagingDesc.MiscFlags=0;stagingDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;Check(consumer.device->CreateTexture2D(&stagingDesc,nullptr,&staging),"Create retained-frame readback");consumer.context->CopyResource(staging.Get(),receiver.Texture());D3D11_MAPPED_SUBRESOURCE data{};Check(consumer.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&data),"Read retained frame");auto result=*static_cast<const uint32_t*>(data.pData);consumer.context->Unmap(staging.Get(),0);return result;};
    create();publish(0xff0022cc);SharedState state{};state.driverPid=1;state.frameHandle=reinterpret_cast<uint64_t>(handle);state.frameGeneration=1;receiver.Receive(state);Require(receiver.HasFrame()&&pixel()==0xff0022cc,"First completed GPU copy becomes displayed frame");const auto stamp=receiver.ReceivedAt();
    receiver.Receive({});Sleep(1100);Require(receiver.HasFrame()&&receiver.ReceivedAt()==stamp&&pixel()==0xff0022cc,"Disconnect and pauses longer than one second retain last image");
    create();state.frameHandle=reinterpret_cast<uint64_t>(handle);++state.frameGeneration;receiver.Receive(state);Require(receiver.HasFrame()&&pixel()==0xff0022cc,"Unpublished replacement cannot erase displayed image");
    publish(0xff0077aa);receiver.Receive(state);Require(pixel()==0xff0077aa,"Replacement becomes visible only after first synchronized copy");
}
void TestPackets() {
    PosePacket p=Packet(); Quaternion q; uint64_t sequence=0;
    auto decode=[&](size_t length=64){return DecodePacket(&p,length,TestToken,0,false,q,sequence);};
    Require(decode() && Near(q.w,1) && sequence==1,"Valid packet normalized");
    Require(!decode(63) && !decode(65),"Exact packet size required");
    Require(!DecodePacket(&p,64,TestToken,1,true,q,sequence),"Reject replay");
    Require(!DecodePacket(&p,64,TestToken,2,true,q,sequence),"Reject out of order");
    p.version=2; Require(!decode(),"Reject unknown version"); p=Packet();
    p.magic[0]='X'; Require(!decode(),"Reject magic"); p=Packet();
    p.token[5]='X'; Require(!decode(),"Reject wrong token"); p=Packet();
    p.w=0; Require(!decode(),"Reject zero quaternion");
    p.w=std::numeric_limits<float>::infinity(); Require(!decode(),"Reject infinite quaternion");
    p.w=std::numeric_limits<float>::quiet_NaN(); Require(!decode(),"Reject NaN quaternion");
    uint32_t random=123456789;
    for (int i=0;i<10000;++i) {
        unsigned char bytes[80];
        for (auto& byte:bytes) {random^=random<<13;random^=random>>17;random^=random<<5;byte=static_cast<unsigned char>(random);}
        Require(!DecodePacket(bytes,static_cast<size_t>(i%81),TestToken,0,false,q,sequence),"Reject malformed random packet");
    }
}
void TestIpc() {
    const std::wstring prefix=L"Local\\FreeLinkVR.Tests."+std::to_wstring(GetCurrentProcessId());
    const std::wstring lock=prefix+L".Lock";
    SharedMemory writer,reader;
    Require(writer.Open(prefix.c_str(),lock.c_str()),"Create test IPC");
    Require(reader.Open(prefix.c_str(),lock.c_str()),"Open same test IPC");
    Require(writer.WithLock([](SharedState& s){s.viewerPid=456;s.orientation={0.5,0.5,0.5,0.5};}),"Write IPC");
    SharedState snapshot{};
    Require(reader.Snapshot(snapshot) && snapshot.viewerPid==456 && Near(snapshot.orientation.y,0.5),"Read IPC");
    // Exercise a second process, not just two handles in one address space.
    wchar_t executable[MAX_PATH]{}; GetModuleFileNameW(nullptr,executable,MAX_PATH);
    std::wstring command=L"\""+std::wstring(executable)+L"\" --ipc-child "+std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    Require(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE,"Launch IPC child");
    DWORD wait=WaitForSingleObject(process.hProcess,5000),exitCode=1;
    if (wait!=WAIT_OBJECT_0) TerminateProcess(process.hProcess,2);
    GetExitCodeProcess(process.hProcess,&exitCode);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    Require(wait==WAIT_OBJECT_0 && exitCode==0,"IPC child completed");
    Require(reader.Snapshot(snapshot) && snapshot.frames==7654,"Cross-process write visible");
}
int IpcChild(const wchar_t* parent) {
    const std::wstring prefix=L"Local\\FreeLinkVR.Tests."+std::wstring(parent),lock=prefix+L".Lock";
    SharedMemory memory;
    Require(memory.Open(prefix.c_str(),lock.c_str()),"Child open IPC");
    SharedState snapshot{};
    Require(memory.Snapshot(snapshot) && snapshot.viewerPid==456,"Child read parent IPC");
    Require(memory.WithLock([](SharedState& s){s.frames=7654;}),"Child write IPC");
    return 0;
}
void TestUdp() {
    WSADATA data{}; Require(WSAStartup(MAKEWORD(2,2),&data)==0,"Test Winsock startup");
    SOCKET sender=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    Require(sender!=INVALID_SOCKET,"Create test sender");
    sockaddr_in destination{}; destination.sin_family=AF_INET; destination.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    // Obtain an unused ephemeral port without relying on a fixed service port.
    SOCKET probe=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    Require(bind(probe,reinterpret_cast<sockaddr*>(&destination),sizeof(destination))==0,"Probe UDP port");
    int size=sizeof(destination); Require(getsockname(probe,reinterpret_cast<sockaddr*>(&destination),&size)==0,"Read probe port");
    closesocket(probe);
    {
        UdpReceiver receiver;
        bool invalidRejected=false;
        try {receiver.Open("127.0.0.1",ntohs(destination.sin_port),"bad");} catch (...) {invalidRejected=true;}
        Require(invalidRejected,"Reject invalid UDP configuration");
        receiver.Open("127.0.0.1",ntohs(destination.sin_port),TestToken);
        auto send=[&](PosePacket p){Require(sendto(sender,reinterpret_cast<const char*>(&p),sizeof(p),0,
            reinterpret_cast<sockaddr*>(&destination),sizeof(destination))==sizeof(p),"Send UDP pose");};
        PosePacket p=Packet(); p.token[0]='X'; send(p);
        Quaternion q;
        Require(!receiver.Poll(q),"UDP rejects wrong token");
        send(Packet(2));
        bool updated=false;
        for(int i=0;i<20 && !updated;++i) {updated=receiver.Poll(q);if(!updated) Sleep(1);}
        Require(updated && Near(q.w,1),"UDP receives and normalizes valid packet");
        send(Packet(2)); send(Packet(1));
        Require(!receiver.Poll(q),"UDP rejects replay / out of order");
        SOCKET other=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP); p=Packet(3);
        sendto(other,reinterpret_cast<const char*>(&p),sizeof(p),0,reinterpret_cast<sockaddr*>(&destination),sizeof(destination));
        Require(!receiver.Poll(q),"UDP pins first authenticated sender endpoint"); closesocket(other);
    }
    closesocket(sender); WSACleanup();
}
void TestGraphics() {
    Graphics producer,consumer; producer.Open(); consumer.Open(&producer.luid);
    D3D11_TEXTURE2D_DESC desc{};desc.Width=16;desc.Height=16;desc.MipLevels=1;desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
    desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    ComPtr<ID3D11Texture2D> output;
    Check(producer.device->CreateTexture2D(&desc,nullptr,&output),"Test shared texture");
    ComPtr<IDXGIResource> resource;Check(output.As(&resource),"Test shared resource");
    HANDLE shared=nullptr;Check(resource->GetSharedHandle(&shared),"Test texture handle");
    ComPtr<ID3D11Texture2D> input;
    Check(consumer.device->OpenSharedResource(shared,IID_PPV_ARGS(&input)),"Test second-device open");
    ComPtr<IDXGIKeyedMutex> writeMutex,readMutex;
    Check(output.As(&writeMutex),"Test writer mutex");Check(input.As(&readMutex),"Test reader mutex");
    desc.MiscFlags=0;desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    Check(consumer.device->CreateTexture2D(&desc,nullptr,&staging),"Test staging texture");
    for (uint32_t frame=0;frame<32;++frame) {
        const uint32_t pixel=0xff000000|frame;
        std::vector<uint32_t> pixels(16*16,pixel);
        Require(writeMutex->AcquireSync(0,100)==S_OK,"Acquire producer key");
        producer.context->UpdateSubresource(output.Get(),0,nullptr,pixels.data(),16*4,0);
        producer.context->Flush();Check(writeMutex->ReleaseSync(1),"Publish texture");
        Require(readMutex->AcquireSync(1,100)==S_OK,"Acquire consumer key");
        consumer.context->CopyResource(staging.Get(),input.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Check(consumer.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Read back GPU frame");
        Require(*static_cast<const uint32_t*>(mapped.pData)==pixel,"GPU frame pixel matches producer");
        consumer.context->Unmap(staging.Get(),0);Check(readMutex->ReleaseSync(0),"Return texture to producer");
    }
}

int wmain(int argc,wchar_t** argv) {
    try {
        if (argc==3 && std::wstring(argv[1])==L"--ipc-child") return IpcChild(argv[2]);
        IsolatedProfile profile(L"core");
        TestSessionRecovery();std::cout<<"PASS Remote Play disconnect/reconnect and capture recovery decisions\n";
        TestLibrary();std::cout<<"PASS VR library gaze, pagination and launch manifest\n";
        TestConfiguration();std::cout<<"PASS settings, shortcut parsing and Steam-compatible IDs\n";
        TestTracking();std::cout<<"PASS frame deadline, mouse coalescing, quaternion, recenter, distortion, tracking timeout\n";
        TestPackets();std::cout<<"PASS packet validation, replays and 10000 malformed packets\n";
        TestIpc();std::cout<<"PASS shared state across processes\n";
        TestUdp();std::cout<<"PASS live loopback UDP and endpoint pinning\n";
        TestGraphics();std::cout<<"PASS 32 D3D11 shared-texture frames across two GPU devices\n";
        TestRetainedFrame();std::cout<<"PASS retained GPU frame across stalls, disconnect and replacement\n";
        return 0;
    } catch (const std::exception& e) {std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
