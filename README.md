**English** | [简体中文](README_zh.md)

# YokaiWatch1Recomp

A native Windows PC static recompilation runtime for *Yo-kai Watch 1* (Nintendo Switch), built upon [peiikai/LASRecomp](https://github.com/peiikai/LASRecomp).

<a href="https://recomplauncher.com"><picture><source media="(prefers-color-scheme: dark)" srcset="https://recomplauncher.com/assets/press/launcher/badge-light.svg"><img alt="Get it on RecompLauncher" src="https://recomplauncher.com/assets/press/launcher/badge-dark.svg" height="48"></picture></a>

---

## Legal & Compliance Notice

> [!IMPORTANT]
> - **Zero Bundled Copyrighted Assets**: This repository and all release packages contain **NO** game images, NSO binaries, RomFS `.fa` archives, game fonts, console firmware, saves, or decryption keys (`prod.keys`).
> - **Not an Emulator**: Using an offline local toolchain (`yw1gen` + `zig`), this project statically recompiles legally extracted Switch game code (`exefs/main` and `exefs/sdk`) from your own copy directly into a native x86-64 DLL (`yw1game_<build16>.dll`) paired with an OpenGL 4.5 renderer and HLE system services.
> - **Pure LGPL/MIT Runtime Dependencies**: Ships with an LGPLv2.1+ FFmpeg build (`--disable-gpl`, zero GPL codecs such as `x264`/`x265`), zlib SDL3, and an MIT/BSD runtime.
> - This project provides **NO** links, guides, or tools for obtaining pirated ROMs or decryption keys.

---

## Supported Game Versions

| Game & Region | Title ID | Main Build ID (first 16 hex) | Support Status | Game Text / Mods |
|---|---|---|---|---|
| **Yo-kai Watch 1 (Nintendo Switch JP v1.3.0)** | `0100C0000CEEA000` | `B6E172353E696E65` | **Tested & Verified** | Built-in Switch text (`game_lang = auto` default) + LayeredFS `mods/` support |
| **Other Revisions (`v1.0.0`, `v1.4.0+`)** | `0100C0000CEEA000` | Pattern-resolved | **Universal Runtime Pattern Resolution** | Automatic function/data offset discovery across Switch builds (`yw1_a.fa` & `romfs/` layouts) |

---

## Key Features

- **OpenGL 4.5 Rendering, Lens Fix & Internal Resolution Scaling**: Hardware OpenGL 4.5 rendering backed by a virtual `1920x1080` framebuffer with selectable internal supersampling (`1x` 1080p / `1.5x` 1620p / `2x` 4K 2160p via `res_scale`), automatic discrete GPU preference (`NvOptimusEnablement` / `AmdPowerXpressRequestHighPerformance`), cross-revision pure OpenGL state-machine fix for the Yo-kai Watch search lens (packed `D24S8` offscreen stencil clear tracking + stencil-write alpha-test suppression, eliminating frame-alternating flicker and the ~38° diagonal cutout across all game versions), Windowed / Borderless / Exclusive Fullscreen modes, `16:9` aspect lock or stretch, and configurable V-Sync (`mailbox` / `fifo` / `immediate`).
- **Native 60 FPS & Variable Game Speed (`1.0x` – `3.0x`)**: Runs Switch JP `v1.3.0` natively at **60 FPS** (default; hot-toggleable with original `30 FPS` via `fps_mode` in Launcher or `F1` Display settings) using dynamic `dt` pacing and a smoothed follow-camera interpolator fix (unverified revisions stay safely at `30 FPS`). Stacks cleanly with `1.0x`, `1.5x`, `2.0x`, and `3.0x` real-time speed (`F3` / `game_speed`) and audio pitch/rate sync while keeping pre-rendered cutscenes at `1.0x`.
- **LayeredFS Mods Overlay (`mods/`, Universal Multi-Language Patch Support)**: Drop third-party Switch `romfs/` translation patches in any language (verified by the community with **English** and **Spanish** translations, and compatible with French, Italian, Chinese, and gameplay/visual mods) into `mods/<mod_name>/romfs/`, toggle them with `mods_enabled = 1`, and swap mods anytime without recompiling or modifying your base game dump.
- **Multilingual High-DPI UI (`F1` Overlay & Launcher)**: Built-in launcher UI and in-game overlay (`F1` or `Minus + Plus`) render cleanly in **English, 简体中文, 繁體中文, 日本語, and 한국어** using Windows DirectWrite/ClearType system CJK fonts alongside bundled SIL OFL fonts.
- **Controller, Keyboard & Mouse-to-Touch Support**: Switch Pro / Nintendo physical (`nintendo`) and Xbox letter (`xbox`) presets (note: `Xbox` maps host controller button positions only and does not replace in-game Switch button icons), full key/pad rebinding, rumble (`rumble_strength`), and direct mouse-to-touch input for menus and touch gameplay. Settings persist in `yokaiwatch1.ini`.
- **Fast 2-Module Self-Build & Save Management**: On first launch, `yw1gen.exe` and the bundled slimmed Zig 0.17.0 compiler recompile `main` and `sdk` concurrently into `yw1game_<build16>.dll`, caching intermediate artifacts in `.gamebuild/` and storing saves in `save/` with automatic backups in `save_backups/`.

---

## System Requirements

- **OS**: Windows 10 / 11 (64-bit)
- **CPU**: x86-64 processor supporting **AVX2** and **FMA** (`x86-64-v3`)
- **GPU**: Discrete or integrated GPU with up-to-date **OpenGL 4.5** drivers
- **Memory & Storage**: 8+ GB RAM recommended; ~1 GB free disk space for cached code generation and compiled modules

---

## Quick Start

### 1. Download and Extract Completely

Download **`YokaiWatch1Recomp-v1.0.0-beta2-windows.zip`** (or the latest release) from the [WenVil/YokaiWatch1Recomp Releases](https://github.com/WenVil/YokaiWatch1Recomp/releases) page (`https://github.com/WenVil/YokaiWatch1Recomp`).

> [!WARNING]
> **Extract the entire `.zip` archive to a writable local folder** (for example, `D:\Games\YokaiWatch1Recomp\`) before running **`YokaiWatch1Recomp.exe`**.  
> **Do NOT launch the executable directly from inside Windows ZIP Explorer preview** — the runtime requires the accompanying DLLs, `zig/`, `helper/yw1gen/`, `gamelib-src/`, and font files beside the `.exe`.

> [!TIP]
> **Upgrading from `v1.0.0-beta1` (No Recompile Needed)**: `v1.0.0-beta2` preserves `yw1game_<build16>.dll` binary compatibility (`GAMELIB_ID`) with `v1.0.0-beta1` — extract and overwrite your existing installation folder to receive the Yo-kai Watch lens OpenGL fix directly without recompiling game modules.

### 2. Prepare Your Own Extracted Game Files

From your own legally dumped Nintendo Switch copy of *Yo-kai Watch 1* (`0100C0000CEEA000`), extract `exefs/` and `romfs/` into a folder (for example using `nxdumptool` on Switch or `hactoolnet` / `nstool` with your own `prod.keys`):

```text
YokaiWatch1_GameFiles/
├── exefs/
│   ├── main
│   ├── main.npdm
│   ├── rtld
│   └── sdk
└── romfs/
    └── yw1_a.fa   (+ any additional romfs/ archives)
```

*(Optional) Adding Mods*: Create `mods/<your_mod_name>/romfs/` inside the program folder (or your configured data folder) and place overriding files matching their relative `romfs/` paths.

### 3. Build & Play

Run **`YokaiWatch1Recomp.exe`**, browse to your `YokaiWatch1_GameFiles/` folder, and click **Build** or **Play**.  
Once the 2-module native build completes, subsequent launches start immediately from the cached `yw1game_<build16>.dll`.

### Default Hotkeys

| Key | Action |INI Key |
|---|---|---|
| `F1` (or `Minus + Plus`) | Open / close in-game settings overlay | `hotkey_menu` |
| `F2` | Cycle performance HUD (`off` / `fps` / `detailed`) | `hotkey_hud` |
| `F3` | Cycle game speed (`1.0x` / `1.5x` / `2.0x` / `3.0x`) | `hotkey_speed` |
| `F11` | Toggle fullscreen | `hotkey_fullscreen` |

---

## Mods & LayeredFS Overlay

### Community Translation & Language Mods

> [!NOTE]
> **Third-Party Community Works**: Translation mods referenced below are created and distributed independently by third-party communities. `YokaiWatch1Recomp` is **NOT** bundled with and does **NOT** distribute any mods or copyrighted game assets.

Because `mods/` implements a general-purpose Switch RomFS LayeredFS overlay, it is **natively compatible with RomFS translation patches in any language with zero per-language adaptation**:
- **Community-Tested Translations**: Both the third-party **English** translation patch and **Spanish** translation patch have been confirmed working in-game.
- **Other Languages & Visual/Gameplay Mods**: **French**, **Italian**, **community Chinese** translations, as well as texture, audio, or gameplay mods packaged with a Switch `romfs/` tree work with the exact same layout. When multiple folders exist under `mods/`, overrides are applied alphabetically by folder name.
- **Verified Third-Party English Mod**: [*YO-KAI WATCH 1 for Switch English Translation* on GameBanana](https://gamebanana.com/mods/516048) (`https://gamebanana.com/mods/516048`) — verified on JP `v1.3.0` with **Release 1.2.0**.

#### 3-Step Installation

1. Download and extract your chosen Switch RomFS translation archive (for example from the GameBanana link above).
2. In the launcher (or Settings -> System / RecompLauncher `mods` shortcut), click **Open Mods Folder** to open `<data_root>/mods/`.
3. Copy the mod folder containing `romfs/` into `<data_root>/mods/<mod_name>/romfs/...` (using a custom name like `en_patch`, or the extracted `0100C0000CEEA000` folder directly). Ensure **Enable mods/** is turned on (for example `Mods: 1 active`), then launch immediately (**switching or removing mods requires zero recompilation**).

#### Valid vs. Invalid Directory Layouts

```text
# CORRECT:
mods/en_patch/romfs/...
mods/0100C0000CEEA000/romfs/...

# WRONG (will NOT load):
mods/romfs/...
mods/en_patch/0100C0000CEEA000/romfs/...
```

---

## Command-Line & Launcher Integration (`RecompLauncher`)

`YokaiWatch1Recomp.exe` ships with a validated [recomplauncher.json](recomplauncher.json) manifest and supports:

| Flag / Environment Variable | Description |
|---|---|
| `--game-dir <path>` | Specify the extracted game folder containing `exefs/` and `romfs/`. |
| `--build-only` | Recompile `yw1game_<build16>.dll` headlessly and exit, printing `progress <0.000~1.000> <stage>` lines on `stdout`. |
| `--play` | Skip the launcher UI and boot straight into the game using `--game-dir` or saved `game_dir`. |
| `RECOMPLAUNCHER_MODE` | `build` / `play` / `launcher` override (`RecompLauncher` integration). |
| `RECOMPLAUNCHER_DATA` / `LARECOMP_DATA_ROOT` | Override the user data root containing `yokaiwatch1.ini`, `save/`, `mods/`, and `.gamebuild/`. |
| `RECOMPLAUNCHER_PAD` | Override initial controller preset (`nintendo`, `xbox`, `playstation`). |
| `YW1_EXP_60FPS` | Force frame-rate override (`1` = native 60 FPS on `v1.3.0`, `0` = original 30 FPS); takes precedence over `fps_mode` in `yokaiwatch1.ini`. |

---

## Building from Source (Windows MSYS2 UCRT64)

See [docs/BUILD-windows.md](docs/BUILD-windows.md) for MSYS2 UCRT64 setup, pure LGPLv2.1+ FFmpeg compilation, Zig slimming, freezing `yw1gen.exe`, and assembling the release archive (`make dist`).

---

## Credits, Attribution & Disclosure

- **Upstream Attribution**: The core static recompilation engine, AArch64 lifter/translator, HLE Switch kernel/FS/audio/input services, and self-build framework are derived from [**peiikai/LASRecomp**](https://github.com/peiikai/LASRecomp) (MIT License).  
  **Please do NOT report issues or open pull requests on `peiikai/LASRecomp` for this project.** Report all *Yo-kai Watch 1* compatibility, OpenGL rendering, build, or runtime issues solely in this repository.
- **Development Transparency**: *Yo-kai Watch 1* reverse-engineering, OpenGL 4.5 compatibility HLE backend, ASTC software decoding, dynamic `dt`/speed-multiplier hooks, LayeredFS mods overlay, pure LGPL dependency packaging, and automated regression testing were **human-led in architecture decisions, test design, and acceptance verification, and developed with AI-assisted coding**.
- **Licenses**: See [LICENSE](LICENSE) and [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
