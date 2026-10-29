# Third-party code

OpenVR headers: ValveSoftware/openvr, commit
0924064316de3effbcd1acf1e309182a2deb1c05.
https://github.com/ValveSoftware/openvr/tree/0924064316de3effbcd1acf1e309182a2deb1c05

The driver uses openvr_driver.h; the dashboard library and runtime test tools
use openvr.h and the user's installed openvr_api.dll. The DLL is not bundled.
License: assets/vendor/openvr/LICENSE in the
source tree and licenses/openvr/LICENSE in the binary package (BSD 3-Clause).

Win32, Winsock, DXGI, D3D11 and D3DCompiler are Windows platform components.
Steam, SteamVR and Steam Link are separate Valve products, not redistributed.

Steam Input diagnostics use locally declared public SteamClient020/SteamInput001
flat ABI signatures and the user's installed Steam/SteamVR Home libraries.
The Steamworks SDK and steamclient64.dll/steam_api64.dll are not bundled.
ABI reference: https://partner.steamgames.com/doc/api/ISteamInput
