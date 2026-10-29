param(
    [ValidateSet('register','unregister','status')][string]$Action = 'register',
    [string]$SteamVRPath,
    [string]$DriverPath
)
$ErrorActionPreference = 'Stop'
if (-not $DriverPath) {
    $packageRoot = Split-Path $PSScriptRoot -Parent
    $packaged = Join-Path $packageRoot 'freelinkvr'
    $sourceBuild = Join-Path (Split-Path $packageRoot -Parent) 'build\freelinkvr'
    $DriverPath = if (Test-Path -LiteralPath $packaged) { $packaged } else { $sourceBuild }
}
if (-not $SteamVRPath) {
    $pathsFile = Join-Path $env:LOCALAPPDATA 'openvr\openvrpaths.vrpath'
    if (Test-Path -LiteralPath $pathsFile) {
        $runtime = (Get-Content -LiteralPath $pathsFile -Raw | ConvertFrom-Json).runtime
        if ($runtime) { $SteamVRPath = $runtime[0] }
    }
}
if (-not $SteamVRPath) {
    $steamPath = Join-Path ${env:ProgramFiles(x86)} 'Steam'
    $libraries = @($steamPath)
    $libraryFile = Join-Path $steamPath 'steamapps\libraryfolders.vdf'
    if (Test-Path -LiteralPath $libraryFile) {
        $content = Get-Content -LiteralPath $libraryFile -Raw
        foreach ($match in [regex]::Matches($content,'"path"\s+"([^"]+)"')) {
            $libraries += $match.Groups[1].Value.Replace('\\','\')
        }
    }
    foreach ($library in ($libraries | Select-Object -Unique)) {
        $candidate = Join-Path $library 'steamapps\common\SteamVR'
        if (Test-Path -LiteralPath (Join-Path $candidate 'bin\win64\vrpathreg.exe')) { $SteamVRPath = $candidate; break }
    }
}
if (-not $SteamVRPath) { throw 'SteamVR was not found. Install it from Steam, or specify -SteamVRPath.' }
$tool = Join-Path $SteamVRPath 'bin\win64\vrpathreg.exe'
if (-not (Test-Path -LiteralPath $tool)) { throw "vrpathreg.exe was not found: $tool" }
if ($Action -eq 'status') {
    & $tool show
    if ($LASTEXITCODE -ne 0) { throw 'Could not read SteamVR paths.' }
    exit 0
}
if (Get-Process vrserver,vrcompositor -ErrorAction SilentlyContinue) { throw 'Close SteamVR before changing driver registration.' }
if (-not (Test-Path -LiteralPath $DriverPath)) { throw "Driver directory does not exist: $DriverPath" }
$DriverPath = (Resolve-Path -LiteralPath $DriverPath).Path
if (-not (Test-Path -LiteralPath (Join-Path $DriverPath 'driver.vrdrivermanifest'))) { throw 'Driver manifest is missing.' }
if ($Action -eq 'register' -and -not (Test-Path -LiteralPath (Join-Path $DriverPath 'bin\win64\driver_freelinkvr.dll'))) {
    throw 'Build the driver before registering it.'
}
$verb = if ($Action -eq 'register') { 'adddriver' } else { 'removedriver' }
& $tool $verb $DriverPath
if ($LASTEXITCODE -ne 0) { throw "SteamVR driver $Action failed." }
Write-Host "Driver $Action completed: $DriverPath"
