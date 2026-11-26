# One-time setup of a Windows machine for building YokaiWatch1Recomp (see docs/BUILD-windows.md).
# Installs MSYS2 (clang, lld, make, SDL3, nasm, pkgconf) and Python 3.12, then creates
# tools/venv (lifter + PyInstaller helper: capstone, unicorn, lz4, pyinstaller). Safe to re-run.
# Pure LGPLv2.1+ FFmpeg is built separately via tools/build_deps_windows.sh (never ship MSYS2 GPL FFmpeg).
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if (-not (Test-Path C:\msys64\usr\bin\pacman.exe)) {
    winget install --id MSYS2.MSYS2 -e --accept-source-agreements --accept-package-agreements --silent
}
$env:MSYSTEM = 'UCRT64'
$env:CHERE_INVOKING = '1'
$bash = 'C:\msys64\usr\bin\bash.exe'
& $bash -lc 'pacman -Syuu --noconfirm' | Out-Null
& $bash -lc 'pacman -Syuu --noconfirm' | Out-Null
$pkgs = 'make', 'mingw-w64-ucrt-x86_64-clang', 'mingw-w64-ucrt-x86_64-lld', 'mingw-w64-ucrt-x86_64-sdl3',
        'mingw-w64-ucrt-x86_64-pkgconf', 'mingw-w64-ucrt-x86_64-gdb',
        'mingw-w64-ucrt-x86_64-nasm'
& $bash -lc "pacman -S --needed --noconfirm $($pkgs -join ' ')"

$py = "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe"
if (-not (Test-Path $py)) {
    winget install --id Python.Python.3.12 -e --scope user --accept-source-agreements --accept-package-agreements --silent
}
if (-not (Test-Path "$root\tools\venv\Scripts\python.exe")) { & $py -m venv "$root\tools\venv" }
& "$root\tools\venv\Scripts\python.exe" -m pip install -q capstone==5.0.9 unicorn==2.1.4 lz4==4.4.5 pyinstaller==6.22.3

git -C $root config core.autocrlf false
git -C $root config core.eol lf
Write-Host 'Done. Build LGPL FFmpeg via tools/build_deps_windows.sh, then run MSYS2 UCRT64: make -j RELEASE=1 DEPS=... dist'
