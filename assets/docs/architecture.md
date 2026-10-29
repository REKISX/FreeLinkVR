# Architecture

FreeLinkVR uses Steam Link as the video transport and input client. Its Windows applications and OpenVR driver provide the bridge.

## Components

- `src/launcher/`: a Win32 settings, installation, and startup interface.
- `src/stream/`: D3D11 stereo output, retained-frame reception, lens correction, local/OBS mono output, input diagnostics, and an additional SteamVR dashboard tab.
- `src/driver/`: a virtual OpenVR headset, shared texture output, and pose publication.
- `src/platform/`: Steam discovery, setup transactions, controller integration, process environments, session observation, and settings.
- `src/common/`: math, IPC, GPU utilities, and KeyValues handling.
- `assets/`: manifests, default settings, the mobile layout, documentation, and licensed OpenVR headers.

The package layout differs from the source layout. The stream and launcher sit beside the `freelinkvr/` driver directory and `steaminput/` controller layout. Driver registration and the Steam shortcut use absolute package paths.

## Input and startup

Steam Link translates phone gyro motion to mouse motion using the installed controller layout. The stream receives relative Raw Input with a legacy mouse fallback. It avoids counting the same movement twice. The driver publishes the result as HMD yaw/pitch; position remains fixed and roll is not supplied.

MobileTouch motion queries returned default/zero sensor values on the tested client. Mouse activity is not reported as proof that raw IMU data was received.

Steam can automatically switch to HMD streaming, which disables remote input. FreeLinkVR starts the Steam client with `-vrdisable` while launching SteamVR separately. The startup order is stream, phone connection, then SteamVR.

The non-Steam shortcut receives a generated AppID. A bootstrap process preserves its SteamGameId but starts the actual viewer with AppID 0 for the input context. A Windows Job Object ties their lifetimes together. Steam/SteamVR subprocesses receive a cleaned environment without the stream's AppID, GameID, or OverlayGameID.

Setup backs up modified configuration, preserves unrelated shortcuts, and rolls changes back on failure. Steam and SteamVR DLLs are loaded from the installed products and are not redistributed.

## Frames and presentation

The driver shares stereo frames through D3D11 and a keyed mutex. The receiver retains its last completed private copy across pauses, missing handles, and incoming texture replacement. A transient delay does not replace an existing image with the startup grid.

The stereo swap chain renders the phone output. Radial correction is applied once, per eye. The driver does not add another distortion pass.

The local mono preview uses a separate window excluded from capture. The OBS window shares the same mono DirectComposition surface and remains eligible for Windows Graphics Capture. Its image comes from SteamVR's native mirror where available. The mono renderer preserves aspect ratio, handles sRGB conversion, and updates at up to 30 FPS.

The local Minimize command hides the mono preview, lowers the drawable stereo window, and suspends focus protection. Activation restores the local view. The current window/capture recovery still has a confirmed black-screen bug; see testing.md. The small uncovered strip beneath the local preview is an attempted mitigation, not a verified fix.

## Scope

The project targets phones whose Steam Link client supports gyro-to-mouse input. It is not specific to an operating-system brand. Camera streaming, positional tracking, and tracked controllers are outside the current implementation.

## References

- https://partner.steamgames.com/doc/api/ISteamInput
- https://partner.steamgames.com/doc/api/ISteamInput#GetMotionData
- https://partner.steamgames.com/doc/api/ISteamRemotePlay
- https://partner.steamgames.com/doc/api/ISteamUtils#SetVRHeadsetStreamingEnabled
