# FreeLinkVR

[![Read in Russian](https://img.shields.io/badge/README-%D0%A0%D1%83%D1%81%D1%81%D0%BA%D0%B8%D0%B9-blue)](README.ru.md)

FreeLinkVR is a free, MIT-licensed bridge from SteamVR on a Windows PC to a phone running Steam Link. It is designed for **phones with a gyroscope and Steam Link**, including Android and iOS devices; it is not tied to one phone brand.

Steam Link must expose a working **gyro-to-mouse** controller layout on the phone. A gyroscope alone does not guarantee client compatibility. See [testing and known issues](assets/docs/testing.md) for the current validation scope.

- **FreeLinkVR.exe**: Settings, Setup / Reinstall, and Start.
- **FreeLinkVR-Stream.exe**: stereo output, head orientation, and the additional SteamVR game-library tab.
- **driver_freelinkvr.dll**: the virtual OpenVR headset driver.

## Project layout

There are three top-level folders:

```text
FreeLinkVR/
├── src/                     Source code, tests, and development tools
│   ├── FreeLinkVR.sln       Native Visual Studio solution
│   ├── FreeLinkVR.props     Shared C++ build settings
│   ├── common/              Graphics, IPC, tracking math, and file formats
│   ├── platform/            Windows and Steam integration, setup, settings
│   ├── launcher/            Desktop launcher
│   ├── stream/              Stereo viewer, input, and VR library
│   ├── driver/              OpenVR headset driver
│   ├── tests/               Automated tests and native diagnostic tools
│   └── tools/               Build, driver registration, and diagnostics
├── assets/                  Runtime resources, documentation, dependencies
│   ├── driver/              Driver manifest and default settings
│   ├── steaminput/          Mobile touch-controller layout
│   ├── docs/                Architecture, testing, and known issues
│   └── vendor/openvr/       OpenVR headers and their license
├── build/                   Ready-to-run binaries and their runtime files
├── CMakeLists.txt
├── README.md
├── README.ru.md
├── LICENSE
├── THIRD_PARTY_NOTICES.md
└── Start.cmd
```

The compiler workspace is a **sibling directory**, such as `C:\FreeLinkVR-build`. Object files, CMake caches, test executables, and test screenshots stay there. The project's `build/` folder contains the distributable application.

## Requirements

- Windows 10 or 11, 64-bit, with a Direct3D 11 GPU.
- Steam installed and signed in; SteamVR installed through Steam. Setup can open its installation.
- SteamVR Home installed: FreeLinkVR uses its existing Steam API wrapper for input integration.
- A phone with Steam Link, a gyroscope, and a client layout capable of gyro-to-mouse input.
- For viewing in a headset: a phone holder with suitable lenses.

Steam, SteamVR, Steam Link, and their DLLs are not included.

## Launch with the launcher

1. Open `build/FreeLinkVR.exe`, or run `Start.cmd` in the project root.
2. Choose **Setup**. If SteamVR is missing, confirm its download in Steam and wait. The launcher continues setup when the runtime becomes available.
3. Setup registers the driver, adds **FreeLinkVR** to Steam, and installs its mobile controller layout. Once complete, the button becomes **Reinstall**.
4. Choose **Start**, then connect the phone through Steam Link and select FreeLinkVR.
5. The stream waits for the phone before starting SteamVR. Keep this order: **FreeLinkVR stream → Steam Link connection → SteamVR**.
6. Rotate the phone. If orientation does not respond, check that the touch controller and gyro-to-mouse mapping are enabled in Steam Link.

Setup and Start may gracefully restart Steam with `-vrdisable`. This keeps Remote Play in normal window-streaming mode and preserves remote input; SteamVR runs separately. Running games are not forcibly terminated. Close them yourself if Steam cannot shut down.

After moving the package, run **Setup** again to update Steam's executable and driver paths. Keep the entire `build/` folder together.

## Launch without the launcher

### After the initial setup

Open Steam, select **FreeLinkVR** in the library, and choose **Play**. You can also select the same shortcut through the phone's Steam Link library. Steam must launch `FreeLinkVR-Stream.exe` so the stream retains its shortcut identity and controller layout.

Start Steam with `-vrdisable` if you have restarted it outside FreeLinkVR. Close an independently started SteamVR session before starting the stream. The stream will start SteamVR after the phone connects.

### Fully launcher-free setup from source

The existing console setup tool calls the same installation and startup functions as the launcher. It is a development tool, not a third production application.

From a Visual Studio developer terminal at the project root:

```powershell
msbuild .\src\tests\tools\freelink_setup_check.vcxproj /m:18 /p:Configuration=Release /p:Platform=x64 /p:CL_MPCount=18
& ..\FreeLinkVR-build\msbuild\tools\Release\freelink_setup_check.exe --install "$PWD\build"
& ..\FreeLinkVR-build\msbuild\tools\Release\freelink_setup_check.exe --start "$PWD\build"
```

If installation returns code **2**, complete the SteamVR download in Steam, then run `--install` again. Use `--status "$PWD\build"` to check setup without changing it. Close the stream and SteamVR before installation.

For driver registration alone, use `src/tools/register-driver.ps1 -Action register`. It also supports `status` and `unregister`. Driver registration alone does not create the Steam shortcut or install the gyro layout.

## Compile

Install **Visual Studio 2022 or 2026** with **Desktop development with C++**, an x64 MSVC toolchain, and a Windows SDK. CMake and Python are not required for the Visual Studio solution.

### Which project should I build?

Open **`src/FreeLinkVR.sln`**, select **Release | x64**, and choose **Build → Build Solution**. Set **FreeLinkVR** as the startup project to open the launcher with F5.

| Project | Purpose | Output |
| --- | --- | --- |
| **FreeLinkVR** | Main application and packaging; builds its dependencies automatically | `build/FreeLinkVR.exe` |
| FreeLinkVR-Stream | Stereo viewer and head tracking | `build/FreeLinkVR-Stream.exe` |
| driver_freelinkvr | Virtual SteamVR headset driver | `build/freelinkvr/bin/win64/driver_freelinkvr.dll` |
| freelink_platform | Shared Windows/Steam integration | Internal static library outside the package |

For the application alone, building the **FreeLinkVR** project also builds the stream, driver, and platform library. You do not need to build or start each dependency manually. The solution's test projects are built by Build Solution; its optional diagnostic tools are not required for normal use. The PowerShell helper below also runs the four checks.

### In Visual Studio

1. Open **`src/FreeLinkVR.sln`** directly.
2. Select **Release | x64**.
3. Choose **Build → Build Solution** (Ctrl+Shift+B).
4. Run `build/FreeLinkVR.exe`, or select **FreeLinkVR** as the startup project.

The solution also provides Debug | x64. The launcher depends on the stream, driver, and platform library; its build step copies all required resources and licenses into `build/`. Only the two production EXEs are placed there.

### From PowerShell

Close FreeLinkVR and SteamVR before building and testing: integration tests share the application's IPC names.

From the project root:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\src\tools\build.ps1 -Jobs 18
```

The helper finds MSBuild, builds the solution in Release, and runs all four local checks. Use `-Configuration Debug` for a Debug build. Compiler files, test executables, symbols, and snapshots stay under the sibling `FreeLinkVR-build/msbuild/` directory.

Alternatively, from a Visual Studio developer terminal:

```powershell
msbuild .\src\FreeLinkVR.sln /m:18 /p:Configuration=Release /p:Platform=x64 /p:CL_MPCount=18
```

This command builds and packages the application; it does not run the test executables. The existing CMake project remains available as an optional Windows build path. No Linux support is implemented.

To distribute the app, copy or zip the **contents of `build/`**, including its driver, controller layout, documentation, and licenses. Re-run Setup from the destination folder before streaming.

## Settings and controls

Settings are stored in `%LOCALAPPDATA%/FreeLinkVR/settings.ini` and apply on the next launch.

| Setting | Values |
| --- | --- |
| Stream resolution | 1280 × 720 or 1920 × 1080 |
| Frame rate | 30 or 60 FPS |
| Interpupillary distance | 55–75 mm |
| Lens distortion | On/off, strength 0–60% |
| Turn sensitivity | 25–200% |
| PC preview | One eye in 16:9, size 50–100% |
| Fullscreen | On/off |

Defaults: 720p, 60 FPS, 64 mm, 20% lens correction, and 85% PC preview size. Lens correction is applied separately to each eye; adjust it to your lenses.

| Input | Action |
| --- | --- |
| Tap the image | Resume tracking; select the gaze target in the FreeLinkVR library |
| L / Y | Open the FreeLinkVR tab in the SteamVR dashboard |
| Enter / A | Select the highlighted game |
| R / X | Recenter |
| M | Toggle mouse capture |
| Esc / B | Pause tracking and release the cursor |
| F11 | Toggle fullscreen |
| F8 | Toggle the PC mono preview |
| + / − | Resize the PC preview |
| F2 | Open input diagnostics |

The gyro mapping is always enabled in the supplied layout; no X-button activation is required. The driver receives **headset orientation**, not controller motion. The FreeLinkVR library is an additional dashboard tab; it does not replace SteamVR's own tabs.

## OBS

Add **Window Capture**, choose **FreeLinkVR - OBS (Mono 16:9)**, and select **Windows 10 (1903 and up)** as the capture method. This is the dedicated mono output. Game Capture of the stream process receives the stereo source.

The PC preview uses SteamVR's native left-eye mirror when available, crops it to 16:9, and updates at up to 30 FPS. It uses bilinear scaling with light sharpening, not an AI upscaler. Enlarging the window does not create additional rendered detail.

## Current limitations

- **Known unresolved bug:** after minimizing or leaving the PC viewer and returning, Steam Link can remain black. Stereo output works before this transition. The recent recovery attempts have not resolved it; investigation is deferred. Keep the viewer visible during a session.
- Tracking currently supports yaw and pitch. There is no roll, positional tracking, or tracked hand/controller implementation.
- The app has no session time limit, but a stable two-hour session has not been verified.
- The FreeLinkVR library can include non-VR Steam shortcuts. SteamVR's native account/library integration remains incomplete with `-vrdisable`.
- Job Simulator starts and submits frames, but a gray scene was reported. Full gameplay also requires positional tracking, a calibrated playspace, and tracked controllers.
- Steam Link does not expose a phone camera stream to this application.
- Wi-Fi, Steam's capture method, and the phone's decoder affect reliability.

Connection and window events are logged to `%LOCALAPPDATA%/FreeLinkVR/session.log`; dashboard events use `library.log` in the same folder. F2 diagnostics write `input-diagnostics.log` next to the stream executable.

## Documentation and licensing

- [Architecture](assets/docs/architecture.md)
- [Testing and known issues](assets/docs/testing.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)

Documentation is stored under `assets/docs/` in the source project and `docs/` in the binary package.

FreeLinkVR was developed with assistance from **OpenAI Codex** for code, troubleshooting, and documentation. This acknowledgement is documentation only; it does not imply endorsement by OpenAI or Valve.

The project remains under the **MIT License**; see [LICENSE](LICENSE). OpenVR headers retain their separate BSD 3-Clause license.
