# ============================================================
#  build.ps1  –  One-shot build script for Tesla Remote
#  Requirements: Visual Studio 2022, vcpkg, CMake 3.20+
#
#  Usage:  .\build.ps1
#    or:   .\build.ps1 -Config Release
# ============================================================
param(
    [string]$Config = "Release",
    [string]$VcpkgRoot = "$env:VCPKG_ROOT"
)

if (-not $VcpkgRoot) {
    # Try common locations
    $candidates = @(
        "C:\vcpkg",
        "C:\tools\vcpkg",
        "$env:USERPROFILE\vcpkg"
    )
    foreach ($c in $candidates) {
        if (Test-Path "$c\vcpkg.exe") { $VcpkgRoot = $c; break }
    }
}

if (-not $VcpkgRoot -or -not (Test-Path "$VcpkgRoot\vcpkg.exe")) {
    Write-Error @"
vcpkg not found!
Install vcpkg:
  git clone https://github.com/microsoft/vcpkg C:\vcpkg
  C:\vcpkg\bootstrap-vcpkg.bat
Then set `$env:VCPKG_ROOT = 'C:\vcpkg'  or pass -VcpkgRoot.
"@
    exit 1
}

Write-Host "Using vcpkg at: $VcpkgRoot" -ForegroundColor Cyan

$toolchain = "$VcpkgRoot\scripts\buildsystems\vcpkg.cmake"
$buildDir  = "build"

New-Item -ItemType Directory -Force $buildDir | Out-Null

Write-Host "`nConfiguring CMake ($Config)..." -ForegroundColor Cyan
cmake -S . -B $buildDir `
      -DCMAKE_BUILD_TYPE=$Config `
      "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
      -DVCPKG_TARGET_TRIPLET=x64-windows

if ($LASTEXITCODE -ne 0) { Write-Error "CMake configure failed"; exit 1 }

Write-Host "`nBuilding..." -ForegroundColor Cyan
cmake --build $buildDir --config $Config --parallel

if ($LASTEXITCODE -ne 0) { Write-Error "Build failed"; exit 1 }

Write-Host "`nBuild complete!" -ForegroundColor Green
Write-Host "  Server (Viewer):  $buildDir\server\$Config\TeslaServer.exe"
Write-Host "  Client (Remote):  $buildDir\client\$Config\TeslaClient.exe"
Write-Host ""
Write-Host "Usage:" -ForegroundColor Yellow
Write-Host "  1. Run TeslaServer.exe on your machine (note your IP)."
Write-Host "  2. Send TeslaClient.exe to the remote user."
Write-Host "  3. Remote user enters your IP in the Client window and clicks CONNECT."
Write-Host "  4. You will see their screen live in the Server window."
