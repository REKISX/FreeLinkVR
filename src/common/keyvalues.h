#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <cctype>

namespace slvr::kv {
struct Node {
    uint8_t type=0;
    std::string key,value;
    uint32_t number=0;
    std::vector<Node> children;
};
inline Node Object(std::string key){Node n;n.key=std::move(key);return n;}
inline Node Text(std::string key,std::string value){Node n;n.type=1;n.key=std::move(key);n.value=std::move(value);return n;}
inline Node Integer(std::string key,uint32_t value){Node n;n.type=2;n.key=std::move(key);n.number=value;return n;}
inline Node* Find(std::vector<Node>& nodes,const std::string& key){for(auto& n:nodes)if(n.key==key)return &n;return nullptr;}
inline const Node* Find(const std::vector<Node>& nodes,const std::string& key){for(const auto& n:nodes)if(n.key==key)return &n;return nullptr;}
inline std::string Value(const std::vector<Node>& nodes,const std::string& key){auto n=Find(nodes,key);return n?n->value:std::string{};}
inline void Put(std::vector<Node>& nodes,Node value){if(auto n=Find(nodes,value.key))*n=std::move(value);else nodes.push_back(std::move(value));}
class TextParser {
    const std::string& data_;size_t at_=0;
    void Space(){while(at_<data_.size()){if(std::isspace(static_cast<unsigned char>(data_[at_]))){++at_;continue;}if(data_.compare(at_,2,"//")==0){while(at_<data_.size()&&data_[at_]!='\n')++at_;continue;}break;}}
    std::string Token(){
        Space();if(at_>=data_.size())throw std::runtime_error("Incomplete VDF");
        std::string s;
        if(data_[at_]=='"'){
            ++at_;while(at_<data_.size()){
                char c=data_[at_++];if(c=='"')return s;
                if(c=='\\'&&at_<data_.size()&&(data_[at_]=='\\'||data_[at_]=='"'))c=data_[at_++];
                s+=c;
            }throw std::runtime_error("Unterminated VDF string");
        }
        while(at_<data_.size()&&!std::isspace(static_cast<unsigned char>(data_[at_]))&&data_[at_]!='{'&&data_[at_]!='}')s+=data_[at_++];
        if(s.empty())throw std::runtime_error("Invalid VDF token");return s;
    }
    std::vector<Node> Read(unsigned depth,bool nested){
        if(depth>64)throw std::runtime_error("VDF nesting limit");std::vector<Node> result;
        while(true){
            Space();if(at_==data_.size()){if(nested)throw std::runtime_error("Unclosed VDF object");return result;}
            if(data_[at_]=='}'){if(!nested)throw std::runtime_error("Unexpected VDF brace");++at_;return result;}
            auto key=Token();Space();if(at_<data_.size()&&data_[at_]=='{'){++at_;auto n=Object(key);n.children=Read(depth+1,true);result.push_back(std::move(n));}
            else result.push_back(Text(key,Token()));
        }
    }
public:
    explicit TextParser(const std::string& data):data_(data){if(data_.size()>16*1024*1024)throw std::runtime_error("VDF size limit");if(data_.compare(0,3,"\xef\xbb\xbf")==0)at_=3;}
    std::vector<Node> Parse(){return Read(0,false);}
};
inline std::string Quote(const std::string& s){std::string result="\"";for(char c:s){if(c=='"'||c=='\\')result+='\\';result+=c;}return result+'"';}
inline std::string EncodeText(const std::vector<Node>& nodes,unsigned depth=0){
    if(depth>64)throw std::runtime_error("VDF nesting limit");std::string result;
    for(const auto& n:nodes){const std::string pad(depth,'\t');result+=pad+Quote(n.key);if(n.type==0)result+="\n"+pad+"{\n"+EncodeText(n.children,depth+1)+pad+"}\n";else result+="\t\t"+Quote(n.type==2?std::to_string(n.number):n.value)+"\n";}return result;
}
class BinaryParser {
    const std::string& data_;size_t at_=0;
    std::string String(){const auto end=data_.find('\0',at_);if(end==std::string::npos)throw std::runtime_error("Truncated shortcut string");auto result=data_.substr(at_,end-at_);at_=end+1;return result;}
    std::vector<Node> Read(unsigned depth){
        if(depth>64)throw std::runtime_error("Shortcut nesting limit");std::vector<Node> result;
        while(at_<data_.size()){
            const auto type=static_cast<uint8_t>(data_[at_++]);if(type==8)return result;
            Node n;n.type=type;n.key=String();
            if(type==0)n.children=Read(depth+1);
            else if(type==1)n.value=String();
            else if(type==2){if(data_.size()-at_<4)throw std::runtime_error("Truncated shortcut integer");std::memcpy(&n.number,data_.data()+at_,4);at_+=4;}
            else throw std::runtime_error("Unsupported shortcut format; existing file was not changed");
            result.push_back(std::move(n));
        }throw std::runtime_error("Missing shortcut terminator");
    }
public:
    explicit BinaryParser(const std::string& data):data_(data){if(data.size()>16*1024*1024)throw std::runtime_error("Shortcut size limit");}
    std::vector<Node> Parse(){auto result=Read(0);if(at_!=data_.size())throw std::runtime_error("Trailing shortcut bytes");return result;}
};
inline std::string EncodeBinary(const std::vector<Node>& nodes,unsigned depth=0){
    if(depth>64)throw std::runtime_error("Shortcut nesting limit");std::string result;
    for(const auto& n:nodes){result+=static_cast<char>(n.type);result+=n.key;result+='\0';
        if(n.type==0)result+=EncodeBinary(n.children,depth+1);
        else if(n.type==1){result+=n.value;result+='\0';}
        else if(n.type==2)result.append(reinterpret_cast<const char*>(&n.number),4);
        else throw std::runtime_error("Unsupported shortcut type");
    }return result+static_cast<char>(8);
}
inline uint32_t ShortcutId(const std::string& quotedExe,const std::string& name){
    uint32_t crc=0xffffffffu;for(unsigned char byte:quotedExe+name){crc^=byte;for(int bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u & (0u-(crc&1u)));}return (~crc)|0x80000000u;
}
}
