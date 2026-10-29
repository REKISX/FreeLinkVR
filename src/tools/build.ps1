param(
    [ValidateRange(1,18)][int]$Jobs = 18,
    [ValidateSet('Release','Debug')][string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (Get-Process FreeLinkVR,FreeLinkVR-Stream,vrserver,vrcompositor -ErrorAction SilentlyContinue) {
    throw 'Close FreeLinkVR and SteamVR before building and running integration tests.'
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio with Desktop development with C++.' }
$msbuild = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'MSBuild/**/Bin/MSBuild.exe') | Select-Object -First 1
if (-not $msbuild) { throw 'MSBuild and the x64 C++ toolchain were not found.' }
& $msbuild (Join-Path $projectRoot 'src/FreeLinkVR.sln') /nologo /v:minimal /nr:false "/m:$Jobs" "/p:Configuration=$Configuration" /p:Platform=x64 "/p:CL_MPCount=$Jobs"
if ($LASTEXITCODE -ne 0) { throw 'Solution build failed.' }
$testRoot = "$projectRoot-build/msbuild/tools/$Configuration"
$testDirectory = "$projectRoot-build/msbuild/test-results/$Configuration"
New-Item -ItemType Directory -Path $testDirectory -Force | Out-Null
Push-Location $testDirectory
try {
    & (Join-Path $testRoot 'slvr_tests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Core integration tests failed.' }
    foreach ($application in @('FreeLinkVR-Stream.exe','FreeLinkVR.exe')) {
        $test = Start-Process -FilePath (Join-Path $projectRoot "build/$application") -ArgumentList '--self-test' -WorkingDirectory $testDirectory -WindowStyle Hidden -Wait -PassThru
        if ($test.ExitCode -ne 0) { throw "Self-test failed: $application" }
    }
    & (Join-Path $testRoot 'slvr_driver_test.exe') (Join-Path $projectRoot 'build/freelinkvr/bin/win64/driver_freelinkvr.dll')
    if ($LASTEXITCODE -ne 0) { throw 'Driver integration test failed.' }
} finally { Pop-Location }
Write-Host 'All four checks passed.'
Write-Host "Ready-to-run package: $projectRoot/build"
