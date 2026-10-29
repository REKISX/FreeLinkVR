param([string]$ProjectRoot = (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent))
$ErrorActionPreference = 'Stop'
$packageRoot = Join-Path $ProjectRoot 'build'
foreach ($directory in @('freelinkvr','steaminput','docs','tools','licenses/openvr')) {
    New-Item -ItemType Directory -Path (Join-Path $packageRoot $directory) -Force | Out-Null
}
Copy-Item -Path (Join-Path $ProjectRoot 'assets/driver/*') -Destination (Join-Path $packageRoot 'freelinkvr') -Recurse -Force
Copy-Item -Path (Join-Path $ProjectRoot 'assets/steaminput/*') -Destination (Join-Path $packageRoot 'steaminput') -Recurse -Force
Copy-Item -Path (Join-Path $ProjectRoot 'assets/docs/*') -Destination (Join-Path $packageRoot 'docs') -Recurse -Force
foreach ($name in @('LICENSE','THIRD_PARTY_NOTICES.md','Start.cmd')) {
    Copy-Item -LiteralPath (Join-Path $ProjectRoot $name) -Destination $packageRoot -Force
}
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'src/tools/register-driver.ps1') -Destination (Join-Path $packageRoot 'tools') -Force
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'assets/vendor/openvr/LICENSE') -Destination (Join-Path $packageRoot 'licenses/openvr') -Force
foreach ($name in @('README.md','README.ru.md')) {
    $readme = [IO.File]::ReadAllText((Join-Path $ProjectRoot $name)).Replace('(assets/docs/','(docs/')
    [IO.File]::WriteAllText((Join-Path $packageRoot $name),$readme,[Text.UTF8Encoding]::new($false))
}
