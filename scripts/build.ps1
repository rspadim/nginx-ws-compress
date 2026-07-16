# Build script for Windows (MSVC)
# Usage: .\scripts\build.ps1 [-nginxSrc <path>]

param(
    [string]$nginxSrc = "$env:USERPROFILE\nginx-src"
)

$moduleDir = "$PSScriptRoot\..\ngx_http_ws_deflate_module"

Write-Host "=== ws_deflate module build ===" -ForegroundColor Green
Write-Host "Module: $moduleDir"
Write-Host "nginx:  $nginxSrc"

# Clone nginx if not present
if (-not (Test-Path $nginxSrc)) {
    Write-Host "Cloning nginx source..."
    git clone https://github.com/nginx/nginx.git $nginxSrc
    Push-Location $nginxSrc
    $latest = git describe --tags --abbrev=0
    git checkout $latest
    Pop-Location
}

Push-Location $nginxSrc

Write-Host "=== Configuring ===" -ForegroundColor Green
$vcpkgInclude = "C:\vcpkg\installed\x64-windows\include"
$vcpkgLib = "C:\vcpkg\installed\x64-windows\lib"

auto\configure `
    --with-cc=cl `
    --with-cc-opt="-I$vcpkgInclude" `
    --with-ld-opt="-LIBPATH:$vcpkgLib" `
    --add-module="$moduleDir"

Write-Host "=== Building ===" -ForegroundColor Green
nmake -f objs\Makefile

Write-Host "=== Done ===" -ForegroundColor Green
Get-ChildItem objs\*.exe, objs\*.dll

Pop-Location
