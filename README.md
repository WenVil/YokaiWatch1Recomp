**English** | [简体中文](README_zh.md)

# 13SentinelsRecomp

A native Windows PC static recompilation runtime for *13 Sentinels: Aegis Rim* (Nintendo Switch), built upon [peiikai/LASRecomp](https://github.com/peiikai/LASRecomp).

<a href="https://recomplauncher.com"><picture><source media="(prefers-color-scheme: dark)" srcset="https://recomplauncher.com/assets/press/launcher/badge-light.svg"><img alt="Get it on RecompLauncher" src="https://recomplauncher.com/assets/press/launcher/badge-dark.svg" height="48"></picture></a>

---

## Legal & Copyright Notice

> [!IMPORTANT]
> - **Zero Bundled Copyrighted Assets**: This repository and all release packages contain **NO** *13 Sentinels: Aegis Rim* game images, NSO binaries, RomFS data, game fonts, console firmware, or decryption keys (`prod.keys`).
> - **Not an Emulator**: Using an offline local toolchain, this project statically recompiles legally extracted Switch game code from your own copy directly into a native x86-64 DLL (`lasgame_<build16>.dll`) and runs it natively on Windows PC with Vulkan 1.3 rendering and HLE system services.
> - This project provides **no** instructions, links, or tools for obtaining copyrighted game ROMs or console keys.

---

## Supported Game Versions

| Region & Version | Title ID | Main Build ID (First 16 Hex) | Status | Built-in Game Languages |
|---|---|---|---|---|
| **Asia v1.00** | `01008D7016438000` | `311947020F64F574` | **Verified** | Simplified Chinese, Traditional Chinese, Korean |
| **US & EU v1.10** | `01003FC01670C000` | `4B632573CA5C160E` | **Verified** | English, French, German, Italian, Spanish |
| **US & EU v1.00** | `01003FC01670C000` | `57212E16CB83BB55` | **Supported (Recommend updating to v1.10)** | English, French, German, Italian, Spanish |
| **Japan (JP)** | — | — | **Not Yet Adapted** | — |

---

## Features

- **Native 60 FPS & Vulkan 1.3 Rendering**: Stable real-time 60 FPS gameplay with windowed, borderless, and exclusive fullscreen modes (`window_mode`), configurable VSync (`mailbox` / `fifo` / `immediate`), and shader pre-warming caches.
- **Internal Resolution Supersampling & Image Quality Options**: Configurable internal render scale (`auto` / `match` / `dynamic` / `25%` / `50%` / `75%` / `1x` / `1.5x` / `2x`), output resolution (`auto` / `720p` / `1080p` / `1440p` / `4k`), FSR 1 upscaling (`fsr1` / `bilinear` / `auto`), and SMAA antialiasing (`off` / `smaa`).
- **5-Language Frontend & Live In-Game Settings Menu**: Both the launcher and the in-game overlay (`F1` or controller `Minus + Plus`) support Simplified Chinese, Traditional Chinese, English, Japanese, and Korean with crisp high-DPI text rendering, alongside in-game language switching saved to `13sentinels.ini`.
- **Full Controller Navigation & Key/Pad Remapping**: Native Nintendo Switch and Xbox button layout presets (`nintendo` / `xbox`), full controller navigation throughout the launcher and settings menu, and full keyboard/mouse/gamepad remapping.
- **Zero-Copyright Local Self-Build & Save Management**: First launch concurrently lifts and compiles the 3 game modules (`main`, `sdk`, `subsdk0`) into `lasgame_<build16>.dll` with incremental build caching, automatic migration from legacy `larecomp.ini` configs, quick save-folder navigation, and one-click save backups (`save_backups/`).

---

## System Requirements

- **OS**: Windows 10 / 11 (64-bit)
- **CPU**: x86-64 processor with **AVX2** and **FMA** support (`x86-64-v3`)
- **GPU**: Dedicated or integrated GPU supporting **Vulkan 1.3** with up-to-date graphics drivers
- **RAM / Storage**: 16 GB RAM recommended (8 GB minimum); ~4–8 GB free disk space required for first-time local recompilation

---

## Quick Start

### Step 1: Download & Fully Extract the Release Archive

Download `13SentinelsRecomp-*-windows.zip` from GitHub Releases.

> [!WARNING]
> **Always extract the entire `.zip` archive to a writable local folder first** (for example `D:\Games\13SentinelsRecomp\`), then open that extracted folder and double-click **`13SentinelsRecomp.exe`**.  
> **Never double-click `13SentinelsRecomp.exe` directly inside a ZIP archive preview window** — doing so prevents Windows from locating the bundled DLLs, Zig toolchain, and `lasgen` code generator, causing startup or build failures.

### Step 2: Prepare Extracted `exefs` & `romfs` Folders

Dump the Switch files from your own legally owned copy of *13 Sentinels: Aegis Rim* so that a single parent directory contains both `exefs/` and `romfs/`. (To avoid requiring players to install the .NET 9.0 Runtime locally, standard release packages omit an `.nsp`/`.xci` extractor and accept extracted `exefs`/`romfs` folders directly.)

Three common open-source methods to extract `exefs` and `romfs` from your own copy:

1. **On Nintendo Switch (`nxdumptool`)**: Dump **ExeFS** and **RomFS** directly from your cartridge or installed digital title on console using `nxdumptool`.
2. **On PC via an Open-Source Switch Emulator (e.g., Ryujinx)**: Load your own legally dumped game in Ryujinx — **for US/EU versions, be sure to mount/apply the v1.10 update patch first** — then right-click the title and extract **ExeFS** and **RomFS** separately into `exefs/` and `romfs/`.
3. **Command-Line Extraction (`hactoolnet` / `nstool`)**: Unpack your own dump using `hactoolnet` (LibHac) or `nstool` together with your own console's `prod.keys`.

> [!NOTE]
> **US & EU v1.10 Base + Update Merge Requirement**: If extracting US/EU v1.10 via CLI tools or patches, ensure the **base game and v1.10 update are merged** so that `exefs/` holds the updated v1.10 `main` binary (Build ID starting with `4B632573CA5C160E`) and `romfs/` includes `ROBO_PATCH_US.CPK`.

Expected folder structure:

```text
13Sentinels_GameFiles/
├── exefs/
│   ├── main
│   ├── main.npdm
│   ├── rtld
│   ├── sdk
│   └── subsdk0
└── romfs/
    └── ROBO_*.CPK   (Asia v1.00: ROBO_CN.CPK, ROBO_KO.CPK, ROBO_TW.CPK;
                      US/EU v1.10: ROBO_US.CPK, ROBO_PATCH_US.CPK, ROBO_FR.CPK, ROBO_GE.CPK, ROBO_IT.CPK, ROBO_SP.CPK)
```

### Step 3: Launch & Run Local Self-Build

Run **`13SentinelsRecomp.exe`** from your extracted folder, select `13Sentinels_GameFiles/` in the launcher, verify that the detected region and version check passes, and click **Build Game Module** or **Start Game**. First-time generation and compilation of all 3 modules takes roughly **6–7 minutes**; subsequent launches reuse cached builds and start within seconds.

### Step 4: Play & Configure Settings

Press **`F1`** (or controller **`Minus + Plus`**) at any time in game to open the settings overlay for graphics, audio, language, and key/controller bindings. All settings are saved to `13sentinels.ini` next to the executable.

---

## CLI Options (`RecompLauncher` Compatible)

Standard CLI flags are supported for batch scripts and third-party launcher integration (see [recomplauncher.json](recomplauncher.json)):

| Flag | Description |
|---|---|
| `--game-dir <path>` | Specify the extracted game root directory containing `exefs/` and `romfs/`. |
| `--build-only` | Headlessly recompile `lasgame_<build16>.dll` and exit upon completion, emitting deduplicated `progress <0.000~1.000> <stage>` status lines on `stdout`. |
| `--play` | Skip the launcher UI and boot directly using the configured or `--game-dir` game folder. |

---

## Building from Source (Windows MSYS2 UCRT64)

Install the MSYS2 UCRT64 toolchain dependencies first (see [docs/BUILD-windows.md](docs/BUILD-windows.md)), then prepare LGPL FFmpeg, Zig, and the frozen code generator:

```bash
bash tools/build_deps_windows.sh
bash tools/fetch_zig.sh
make -j8 GEN_PY=/path/to/python.exe gen-helper
make -j8 dist
```

The distributable archive is written to `build/dist/13SentinelsRecomp-v1.0.0-beta1-windows.zip`.

---

## Credits, Attribution & Disclosure

- **Upstream Attribution**: The core static recompilation runtime, AArch64 instruction lifter/translator, HLE system services, and NVN-to-Vulkan backend are derived from [**peiikai/LASRecomp**](https://github.com/peiikai/LASRecomp) (MIT License).  
  **Please do NOT report issues or open pull requests on `peiikai/LASRecomp` for this project.** Report any *13 Sentinels: Aegis Rim* compatibility, build, or runtime issues solely in this repository.
- **Development Transparency**: Adaptation analysis for *13 Sentinels: Aegis Rim*, multilingual UI overhaul, 3-module self-build pipeline, jump-table hardening, and end-to-end testing were **human-led in architecture decisions, test design, and acceptance verification, and developed with AI-assisted coding**.
- **Licenses**: See [LICENSE](LICENSE) and [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
