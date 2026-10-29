#pragma once
#include <windows.h>
#include <cstdint>
#include <stdexcept>
#include "common/tracking.h"

namespace slvr {
constexpr uint32_t IpcMagic = 0x52564c53, IpcVersion = 1;
constexpr wchar_t MappingName[] = L"Local\\FreeLinkVR.State.v1";
constexpr wchar_t MutexName[] = L"Local\\FreeLinkVR.StateLock.v1";
constexpr wchar_t ViewerMutexName[] = L"Local\\FreeLinkVR.Viewer.v1";
constexpr uint64_t PoseTimeoutMs = 500;
enum class DriverStatus : uint32_t { Offline, Ready, Streaming, GraphicsError };
struct SharedState {
    uint32_t magic, version;
    uint32_t viewerPid, driverPid;
    uint64_t poseTimeMs;
    Quaternion orientation;
    uint64_t frameHandle, frameGeneration;
    uint32_t width, height;
    LUID adapterLuid;
    uint64_t frames, frameTimeMs;
    DriverStatus status;
    uint32_t reserved;
};
inline bool PoseFresh(const SharedState& state, uint64_t now) {
    return state.viewerPid != 0 && state.poseTimeMs != 0 && now >= state.poseTimeMs &&
           now-state.poseTimeMs <= PoseTimeoutMs;
}
class SharedMemory {
    HANDLE mutex_ = nullptr, mapping_ = nullptr;
    SharedState* state_ = nullptr;
public:
    SharedMemory() = default;
    SharedMemory(const SharedMemory&) = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;
    ~SharedMemory() { Close(); }
    void Close() {
        if (state_) UnmapViewOfFile(state_);
        if (mapping_) CloseHandle(mapping_);
        if (mutex_) CloseHandle(mutex_);
        state_ = nullptr; mapping_ = nullptr; mutex_ = nullptr;
    }
    bool Open(const wchar_t* mappingName=MappingName,const wchar_t* mutexName=MutexName) {
        Close();
        mutex_ = CreateMutexW(nullptr,FALSE,mutexName);
        if (!mutex_) return false;
        const DWORD wait = WaitForSingleObject(mutex_,2000);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) { Close(); return false; }
        mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(SharedState),mappingName);
        const bool created = mapping_ && GetLastError() != ERROR_ALREADY_EXISTS;
        if (mapping_) state_ = static_cast<SharedState*>(MapViewOfFile(mapping_,FILE_MAP_ALL_ACCESS,0,0,sizeof(SharedState)));
        if (state_ && created) {
            *state_ = {};
            state_->magic = IpcMagic; state_->version = IpcVersion;
            state_->orientation = {};
        }
        const bool ok = state_ && state_->magic == IpcMagic && state_->version == IpcVersion;
        ReleaseMutex(mutex_);
        if (!ok) Close();
        return ok;
    }
    template<class Fn> bool WithLock(Fn fn, DWORD timeout = 0) {
        if (!state_) return false;
        const DWORD wait = WaitForSingleObject(mutex_,timeout);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) return false;
        // Callbacks only copy small POD values and must not throw or perform GPU/network work.
        fn(*state_);
        ReleaseMutex(mutex_);
        return true;
    }
    bool Snapshot(SharedState& state) { return WithLock([&](const SharedState& s){state=s;}); }
};
}
