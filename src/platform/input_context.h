#pragma once
#include <windows.h>
#include <optional>
#include <string>
#include <vector>

namespace slvr {
std::vector<wchar_t> HardwareInputEnvironment(std::vector<std::wstring> variables);
std::vector<wchar_t> StandaloneSteamEnvironment(std::vector<std::wstring> variables);
// Non-Steam shortcuts receive a generated AppID that Steamworks rejects. Keep
// their SteamGameId, but launch the actual window in the AppID-0 hardware context.
// Returns an exit code in the bootstrapper; nullopt in the actual window process.
std::optional<int> BootstrapInputContext();
}
