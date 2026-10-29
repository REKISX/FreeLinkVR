#pragma once
#include "platform/setup.h"
#include <sstream>
#include <stdexcept>
namespace slvr {
inline std::string JsonString(const std::string& text){
    std::string result="\"";constexpr char hex[]="0123456789abcdef";
    for(unsigned char c:text){
        if(c=='"'||c=='\\'){result+='\\';result+=static_cast<char>(c);}
        else if(c<32){result+="\\u00";result+=hex[c>>4];result+=hex[c&15];}
        else result+=static_cast<char>(c);
    }return result+'"';
}
inline std::string GameKey(uint64_t id){
    if(!id)throw std::runtime_error("Invalid game ID");
    return "steam.app."+std::to_string(id>0xffffffffULL?id>>32:id);
}
inline std::string GameManifest(const std::vector<SteamGame>& games){
    std::ostringstream result;result<<"{\"applications\":[";bool first=true;
    for(const auto& game:games){
        if(!first)result<<',';first=false;
        result<<"{\"app_key\":"<<JsonString(GameKey(game.id))
              <<",\"launch_type\":\"url\",\"url\":\"steam://rungameid/"<<game.id
              <<"\",\"strings\":{\"en_us\":{\"name\":"<<JsonString(Utf8(game.name))<<"}}}";
    }return result.str()+"]}";
}
}
