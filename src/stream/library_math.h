#pragma once
#include <optional>
#include <cmath>
#include <cstddef>
namespace slvr {
struct GazePoint {float x=0,y=0;};
inline std::optional<GazePoint> LibraryGaze(float u,float v){
    if(!std::isfinite(u)||!std::isfinite(v)||u<0||u>1||v<0||v>1)return {};
    // OpenVR overlay coordinates start at the lower left; our bitmap is top-down.
    return GazePoint{u,1-v};
}
constexpr int LibraryWidth=1024,LibraryHeight=768,LibraryRows=6;
inline int LibraryHit(float x,float y,size_t count,size_t page){
    if(!std::isfinite(x)||!std::isfinite(y)||x<0||x>1||y<0||y>1)return -1;
    const float px=x*LibraryWidth,py=y*LibraryHeight;
    if(px>=32&&px<=992&&py>=136&&py<136+LibraryRows*78){
        const auto row=static_cast<size_t>((py-136)/78);const size_t index=page*LibraryRows+row;
        return index<count?static_cast<int>(index):-1;
    }
    if(py>=650&&py<=718){
        if(px>=32&&px<=262&&page>0)return -2;
        if(px>=762&&px<=992&&(page+1)*LibraryRows<count)return -3;
    }
    if(px>=800&&px<=992&&py>=36&&py<=90)return -5;
    return -1;
}
}
