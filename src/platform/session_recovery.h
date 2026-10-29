#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace slvr {
// A new Remote Play session may replace the old one between two polls, without
// an observable disconnected interval. Controller identities are the fallback.
class PeerSession {
    bool connected_=false,seen_=false,pending_=false;
    uint64_t identity_=0;
public:
    bool Connected()const{return connected_;}
    void Observe(std::vector<uint32_t> sessions,std::vector<uint64_t> mobile){
        std::sort(sessions.begin(),sessions.end());std::sort(mobile.begin(),mobile.end());
        const bool connected=!sessions.empty()||!mobile.empty();
        uint64_t identity=14695981039346656037ULL;
        auto mix=[&](uint64_t value){for(int i=0;i<8;++i){identity^=value&255;identity*=1099511628211ULL;value>>=8;}};
        if(!sessions.empty()){mix(1);for(auto id:sessions)mix(id);}
        else{mix(2);for(auto id:mobile)mix(id);}
        if(connected&&seen_&&(!connected_||identity!=identity_))pending_=true;
        if(connected)seen_=true;
        connected_=connected;if(connected)identity_=identity;
    }
    bool TakeReconnect(){const bool value=pending_;pending_=false;return value;}
};
}
