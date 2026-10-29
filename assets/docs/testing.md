# Testing and known issues

## Validation scope

The phone runtime was exercised with an iPhone 8 running iOS 16. The project is designed for any phone with a gyroscope and a Steam Link client that supports the required gyro-to-mouse layout; other phones have not been validated here.

The user confirmed gyro-driven head rotation, automatic SteamVR startup after connecting the phone, retained frames without repeated grid flashes, and stereo phone output with a separate mono PC preview.

A stable two-hour session and optical calibration for specific lenses have not been verified.

## Automated checks

Run the native MSBuild helper in `src/tools/build.ps1`. It builds `src/FreeLinkVR.sln` and runs all four checks without CMake. The existing CMake project also exposes the same checks through CTest. Close the production stream and SteamVR first because some integration tests use production IPC names.

The four checks cover:

1. Orientation math, recentering, pose expiry, input fallback without double counting, UDP validation, replay rejection, malformed packets, settings bounds, configuration parsing, safe manifests, environment isolation, and session identity changes.
2. Hidden-viewer shader compilation and GPU readback, stereo-eye isolation, mono crop/margins and gamma handling, retained frames, local park/restore, and buffer refresh without replacing the OBS window.
3. Creation of the launcher and settings window, with generated previews.
4. Driver DLL integration with a test OpenVR host, configured output size, stereo halves, pose publication/expiry, synchronization, and cleanup.

These checks validate local behavior. They cannot establish that Steam Link resumes a real phone's video capture.

After the three-folder reorganization and English translation, the Release build and all four CTest entries passed. The translated launcher and settings window were also visually inspected. The black-screen investigation remains deferred.

Native test and diagnostic executables are built under `FreeLinkVR-build/msbuild/tools/<Configuration>/`. The setup/runtime probes and manual scene have their own Visual Studio projects and are not selected by the solution's default build. The runtime probe requires an existing viewer and driver so it does not accidentally start SteamVR out of order.

## Confirmed unresolved black-screen bug

After leaving or minimizing the PC viewer and returning, the phone can remain black. The latest user check confirmed stereo before the transition and a black image afterward, including after explicitly restoring the actual stream window.

Investigation has been deferred at the user's request. Do not describe this as fixed.

Observations:

- The stream can continue receiving SteamVR frames and report successful D3D11 Present calls while the phone is black.
- Steam can switch from game capture to Desktop DWM capture and retain the same Remote Play session ID.
- Reconnection detection, buffer refresh, keeping the source drawable behind other windows, and a two-row uncovered strip have not resolved the end-to-end symptom.
- Replacing a Steam-hooked swap chain caused a DXGI access violation during a runtime check. Recovery now preserves the object and refreshes its buffers.
- Rendering mono directly inside the capture HWND removed the black symptom in one user check but also sent mono to the phone through desktop capture. That variant was reverted.

Keep the viewer visible for current sessions. Restart the stream if capture becomes stuck.

## Other limits

- Phone gyro is mapped to mouse motion. Direct raw sensor transmission remains unconfirmed.
- Yaw/pitch are supported; positional tracking, roll, and tracked controllers are absent.
- SteamVR's own account/library integration is incomplete in Steam's window-streaming mode.
- Job Simulator launches and submits frames, but a gray scene was reported. Playspace calibration errors were seen, and the final cause is unconfirmed. It also needs tracking capabilities this version does not implement.
- Intermittent edge remnants during fast turns were reported on both PC and phone. No root cause has been established.
- A clean SteamVR download flow was not exercised because the runtime was already installed.

The standalone Visual Studio solution in src/FreeLinkVR.sln was built with MSBuild in Release | x64, and all four checks passed. Its native packaging step copies runtime resources without invoking CMake.
