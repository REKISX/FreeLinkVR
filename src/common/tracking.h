#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <chrono>
#include <utility>

namespace slvr {
constexpr double Pi = 3.14159265358979323846;
inline std::chrono::steady_clock::time_point NextVsync(
    std::chrono::steady_clock::time_point epoch,std::chrono::steady_clock::time_point submit,double hz) {
    const double elapsed=std::chrono::duration<double>(submit-epoch).count();
    return epoch+std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>((std::floor(elapsed*hz)+1)/hz));
}
struct MouseMotion {
    int64_t rawX=0,rawY=0;
    int legacyX=0,legacyY=0;
    bool rawSeen=false;
    void AddRaw(int x,int y) {if(x || y){rawX+=x;rawY+=y;rawSeen=true;}}
    void SetLegacy(int x,int y) {if(x || y){legacyX=x;legacyY=y;}}
    void AddLegacyDelta(int x,int y) {legacyX+=x;legacyY+=y;}
    std::pair<double,double> Take() {
        const std::pair<double,double> delta=rawSeen
            ? std::make_pair(static_cast<double>(rawX),static_cast<double>(rawY))
            : std::make_pair(static_cast<double>(legacyX),static_cast<double>(legacyY));
        *this={};return delta;
    }
};
struct Quaternion {
    double w = 1, x = 0, y = 0, z = 0;
};
inline bool Normalize(Quaternion& q) {
    const double n = q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z;
    if (!std::isfinite(n) || n < 1e-12 || n > 1e12) return false;
    const double s = 1 / std::sqrt(n);
    q = {q.w*s, q.x*s, q.y*s, q.z*s};
    return true;
}
inline Quaternion Multiply(Quaternion a, Quaternion b) {
    return {a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z,
            a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
            a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
            a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w};
}
inline Quaternion Inverse(Quaternion q) { return {q.w,-q.x,-q.y,-q.z}; }
inline Quaternion FromYawPitch(double yaw, double pitch) {
    return Multiply({std::cos(yaw/2),0,std::sin(yaw/2),0},
                    {std::cos(pitch/2),std::sin(pitch/2),0,0});
}
inline void Distort(float u, float v, float k1, float k2, float& outU, float& outV) {
    const float x = 2*u-1, y = 2*v-1;
    const float r2 = x*x+y*y;
    const float scale = 1+k1*r2+k2*r2*r2;
    outU = (x*scale+1)/2; outV = (y*scale+1)/2;
}
// Wire format, little endian, exactly 64 bytes. Quaternion is in OpenVR axes:
// +X right, +Y up, -Z forward; components w,x,y,z. Position is intentionally fixed.
// Magic "SLVR", version u32=1, token[32] (32 ASCII hex digits), sequence u64,
// and four IEEE754 float32 quaternion components. Receiver timestamps locally.
struct PosePacket {
    char magic[4];
    uint32_t version;
    char token[32];
    uint64_t sequence;
    float w,x,y,z;
};
static_assert(sizeof(PosePacket) == 64);
inline bool DecodePacket(const void* bytes, size_t length, std::string_view token,
                         uint64_t lastSequence, bool haveSequence,
                         Quaternion& q, uint64_t& sequence) {
    if (length != sizeof(PosePacket) || token.size() != 32) return false;
    PosePacket p{};
    std::memcpy(&p, bytes, sizeof(p));
    if (std::memcmp(p.magic,"SLVR",4) || p.version != 1 ||
        std::memcmp(p.token,token.data(),32) || (haveSequence && p.sequence <= lastSequence)) return false;
    q = {p.w,p.x,p.y,p.z};
    if (!Normalize(q)) return false;
    sequence = p.sequence;
    return true;
}
}
