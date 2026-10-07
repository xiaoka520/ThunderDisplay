param([string]$Configuration = "Release")
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
cmake -S $Root -B "$Root/build/windows" -A x64
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
cmake --build "$Root/build/windows" --config $Configuration
if ($LASTEXITCODE -ne 0) { throw "Windows build failed" }
ctest --test-dir "$Root/build/windows" -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Protocol tests failed" }
New-Item -ItemType Directory -Force "$Root/build/package/windows-x64/licenses" | Out-Null
Copy-Item "$Root/build/windows/windows-client/$Configuration/ThunderDisplayClient.exe" "$Root/build/package/windows-x64/"
Copy-Item "$Root/windows-client/START-HERE.txt" "$Root/build/package/windows-x64/"
Copy-Item "$Root/windows-client/assets/README.md" "$Root/build/package/windows-x64/licenses/Mac-system-cursor.txt"
python "$PSScriptRoot/package-windows.py"
if ($LASTEXITCODE -ne 0) { throw "Windows installer build failed; install NSIS 3 and add makensis to PATH" }
