param([string]$Configuration = "Release")
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
cmake -S $Root -B "$Root/build/windows" -A x64
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
cmake --build "$Root/build/windows" --config $Configuration
if ($LASTEXITCODE -ne 0) { throw "Windows build failed" }
ctest --test-dir "$Root/build/windows" -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Protocol tests failed" }
New-Item -ItemType Directory -Force "$Root/dist/windows-x64" | Out-Null
Copy-Item "$Root/build/windows/windows-client/$Configuration/ThunderDisplayClient.exe" "$Root/dist/windows-x64/"
Write-Host "Built $Root/dist/windows-x64/ThunderDisplayClient.exe"
