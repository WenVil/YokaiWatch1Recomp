# Building 13SentinelsRecomp on Windows

`13SentinelsRecomp` targets **Windows 10 / 11 (x86-64)** using **MSYS2 UCRT64** (`clang` + `lld` + Vulkan 1.3), producing `13SentinelsRecomp.exe` and a self-contained zero-copyright distributable archive (`13SentinelsRecomp-<version>-windows.zip`).

---

## 1. Prerequisites

### MSYS2 UCRT64 Packages

Install MSYS2 and run the following inside an **MSYS2 UCRT64** shell:

```bash
pacman -Syuu --noconfirm
pacman -S --needed --noconfirm \
  make \
  mingw-w64-ucrt-x86_64-clang \
  mingw-w64-ucrt-x86_64-lld \
  mingw-w64-ucrt-x86_64-sdl3 \
  mingw-w64-ucrt-x86_64-ffmpeg \
  mingw-w64-ucrt-x86_64-vulkan-devel \
  mingw-w64-ucrt-x86_64-shaderc \
  mingw-w64-ucrt-x86_64-spirv-tools \
  mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-gdb \
  mingw-w64-ucrt-x86_64-vulkan-memory-allocator \
  mingw-w64-ucrt-x86_64-vulkan-validation-layers \
  mingw-w64-ucrt-x86_64-nasm
```

### Python 3.12 (Official CPython for Freezing `lasgen`)

MSYS2 Python lacks prebuilt wheels for `capstone` and `unicorn`, so freezing the standalone code generator (`lasgen.exe`) uses Windows CPython 3.12 with:

- `pyinstaller==6.22.3`
- `capstone==5.0.9`
- `unicorn==2.1.4`
- `lz4==4.4.5`

### Git Line Endings

Shell scripts under `tools/` require LF endings. Configure Git in the repository clone:

```bash
git config core.autocrlf false
git config core.eol lf
```

---

## 2. Quick Development Build (Runtime Only)

In an **MSYS2 UCRT64** shell at the repository root:

```bash
make -j8
```

This builds `build/13SentinelsRecomp.exe` (and an optional dev-only compatibility shim `build/lasrecomp.exe`, which is omitted from release zips) linked against MSYS2's UCRT64 libraries.

---

## 3. Building the Distributable Release Archive (`make dist`)

Release packaging bundles an **LGPL-only** FFmpeg build (instead of MSYS2's GPLv3 FFmpeg), the pinned slimmed Zig toolchain (`zig/`), and the frozen `lasgen/` code generator so end users do not need MSYS2 or Python installed:

```bash
# 1. Build LGPL FFmpeg 9.0.2 into build/deps-win
bash tools/build_deps_windows.sh

# 2. Download and verify the pinned Zig 0.17 Windows toolchain into build/zig
bash tools/fetch_zig.sh

# 3. Freeze the standalone code generator into build/helper/lasgen/
#    (Omit GEN_PY=... to auto-create build/rando-venv using `py -3.12`)
make -j8 GEN_PY=/c/Users/<you>/AppData/Local/Programs/Python/Python312/python.exe gen-helper

# 4. Build the release binary (RELEASE=1, no console window) and zip package
make -j8 dist
```

The packaged output is written to:

- Folder: `build/dist/13SentinelsRecomp/`
- Archive: `build/dist/13SentinelsRecomp-<version>-windows.zip` (e.g., `13SentinelsRecomp-v1.0.0-beta1-windows.zip`)

### Package Contents & Privacy Checks

`make dist` verifies that:

- No game functions (`f_70*`) are linked into `13SentinelsRecomp.exe`.
- No GPL video codec libraries (`x264`, `x265`, `xvid`, `postproc`) are included.
- Local workspace and `%USERPROFILE%` paths are stripped/absent from distributed binaries and DLLs.
- `vulkan-1.dll` is intentionally omitted so the host GPU driver's Vulkan 1.3 loader is used.

---

## 4. Runtime Output & Logs

At runtime, `13SentinelsRecomp.exe` keeps user data alongside the executable:

- Built game module: `lasgame_<build16>.dll` (compiled from `exefs/` via `lasgen/` and `zig/`)
- Configuration: `13sentinels.ini` (automatically migrates settings if a legacy `larecomp.ini` exists)
- Logs (`RELEASE=1`): `13sentinelsrecomp.log` and up to five rotated `13sentinelsrecomp-backup-*.log` files
- Backups: `save_backups/`
