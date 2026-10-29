#pragma once
#include <memory>
#include <filesystem>
#include "common/graphics.h"
#include "common/ipc.h"
namespace slvr {
class VRLibrary {
    struct State;std::unique_ptr<State> state_;
public:
    explicit VRLibrary(Graphics& graphics);
    ~VRLibrary();
    void Tick(HWND window,const SharedState& shared,bool allowFocus=true);
    bool Visible() const;
    uint32_t SceneProcess() const;
    ID3D11ShaderResourceView* MonitorFrame();
    void Open();
    void Select();
    void ResetGraphics();
    static void Preview(const std::filesystem::path& output);
};
}
