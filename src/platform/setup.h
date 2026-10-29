#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>

namespace slvr {
std::wstring Wide(const std::string& text);
std::string Utf8(const std::wstring& text);
std::filesystem::path AppDirectory();
std::filesystem::path FindSteam();
std::filesystem::path FindSteamVR();
bool SteamRunning();
bool SteamWindowMode();
bool IsSetupComplete(const std::filesystem::path& package);
struct SetupResult {bool complete=false,waitingForDownload=false;std::wstring message;};
SetupResult Install(const std::filesystem::path& package);
void StartSession(const std::filesystem::path& package);
HANDLE StartSteamVR();
struct SteamGame {uint64_t id=0;std::wstring name;};
std::vector<SteamGame> SteamGames();
void LaunchSteamGame(uint64_t id);
uint64_t GameId(const std::filesystem::path& streamExe);
std::vector<wchar_t> CleanSteamEnvironment();
}
