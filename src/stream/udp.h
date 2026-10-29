#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include "common/tracking.h"
#include <stdexcept>
#include <string>

namespace slvr {
class UdpReceiver {
    SOCKET socket_=INVALID_SOCKET;
    bool started_=false, haveSequence_=false, havePeer_=false;
    uint64_t sequence_=0;
    sockaddr_in peer_{};
    std::string token_;
public:
    UdpReceiver()=default;
    UdpReceiver(const UdpReceiver&)=delete;
    UdpReceiver& operator=(const UdpReceiver&)=delete;
    ~UdpReceiver() {Close();}
    void Close() {
        if (socket_!=INVALID_SOCKET) closesocket(socket_);
        if (started_) WSACleanup();
        socket_=INVALID_SOCKET; started_=false; haveSequence_=false; havePeer_=false; sequence_=0;
    }
    void Open(const std::string& address,uint16_t port,const std::string& token) {
        Close();
        if (!port || token.size()!=32 || token.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos)
            throw std::runtime_error("UDP requires port 1..65535 and a 32-digit hex token");
        sockaddr_in local{}; local.sin_family=AF_INET; local.sin_port=htons(port);
        if (InetPtonA(AF_INET,address.c_str(),&local.sin_addr)!=1)
            throw std::runtime_error("--listen requires an IPv4 address (127.0.0.1 or a LAN address)");
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2,2),&data)) throw std::runtime_error("WSAStartup failed");
        started_=true;
        socket_=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        if (socket_==INVALID_SOCKET) throw std::runtime_error("UDP socket creation failed");
        BOOL exclusive=TRUE;
        if (setsockopt(socket_,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&exclusive),sizeof(exclusive)) ||
            bind(socket_,reinterpret_cast<sockaddr*>(&local),sizeof(local)))
            throw std::runtime_error("Cannot bind UDP port (address unavailable or port already in use)");
        u_long nonblocking=1;
        if (ioctlsocket(socket_,FIONBIO,&nonblocking)) throw std::runtime_error("Cannot make UDP socket nonblocking");
        token_=token;
    }
    bool Poll(Quaternion& latest) {
        bool updated=false;
        // Bound work per video frame even if the endpoint is flooded.
        for (int i=0;i<128 && socket_!=INVALID_SOCKET;++i) {
            char bytes[256]; sockaddr_in from{}; int length=sizeof(from);
            const int count=recvfrom(socket_,bytes,sizeof(bytes),0,reinterpret_cast<sockaddr*>(&from),&length);
            if (count==SOCKET_ERROR) {
                const int error=WSAGetLastError();
                if (error==WSAEMSGSIZE) continue;
                if (error==WSAEWOULDBLOCK) break;
                throw std::runtime_error("UDP receive failed: "+std::to_string(error));
            }
            if (havePeer_ && (peer_.sin_addr.s_addr!=from.sin_addr.s_addr || peer_.sin_port!=from.sin_port)) continue;
            Quaternion q; uint64_t sequence=0;
            if (DecodePacket(bytes,static_cast<size_t>(count),token_,sequence_,haveSequence_,q,sequence)) {
                peer_=from; havePeer_=true; sequence_=sequence; haveSequence_=true;
                latest=q; updated=true;
            }
        }
        return updated;
    }
};
}
