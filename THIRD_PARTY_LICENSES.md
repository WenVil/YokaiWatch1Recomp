# Third-Party Licenses & Attributions

`YokaiWatch1Recomp` does **not** include any game binaries, RomFS `.fa` assets, firmware, saves, or decryption keys. The runtime and build tools incorporate or dynamically load the open-source components listed below.

---

## Runtime & Static Recompilation Core

| Component | Version | License | Source / Repository | Notes |
|---|---|---|---|---|
| **LASRecomp** (upstream base) | 2026 | **MIT** | https://github.com/peiikai/LASRecomp | AArch64 static recompilation engine, HLE services framework, and self-build cache (`Copyright (c) 2026 Peiikai`). |
| **SDL3** (`SDL3.dll`) | 3.4.x | **zlib License** | https://github.com/libsdl-org/SDL | Windowing, OpenGL context creation, audio output, and controller/keyboard/mouse input. |
| **FFmpeg** (`avcodec-63.dll`, `avformat-63.dll`, `avutil-61.dll`, `swscale-10.dll`) | 9.0.2 | **LGPL-2.1-or-later** | https://ffmpeg.org/ | Pre-rendered video decoding (VP8 / VP9 / H.264 / Opus / Vorbis / AAC) compiled cleanly with `--disable-gpl` via `tools/build_deps_windows.sh` (zero GPL codecs such as `x264` or `x265`). |
| **Dear ImGui** | vendored (`third_party/imgui/`) | **MIT** | https://github.com/ocornut/imgui | OpenGL3 + SDL3 launcher UI and in-game `F1` settings overlay. |
| **Ryujinx ASTC Decoder** | `runtime/gl/astc.c` | **MIT** | https://github.com/Ryujinx/Ryujinx | Software ASTC block decoder for compressed Switch textures on desktop OpenGL (`Copyright (c) Ryujinx Team and Contributors`). |
| **GNU libiconv / MinGW-w64 / GCC Runtime** (`libiconv-2.dll`, `libgcc_s_seh-1.dll`, `libstdc++-6.dll`, `libwinpthread-1.dll`) | MSYS2 UCRT64 | **LGPL-2.1+** / **GPL-3.0 + GCC Runtime Library Exception** / **MIT** | https://packages.msys2.org | Minimal C/C++/POSIX runtime dependencies for Windows x86-64 builds. |

---

## Bundled Self-Build Toolchain (`zig/` & `helper/yw1gen/`)

| Component | Version | License | Source / Repository | Notes |
|---|---|---|---|---|
| **Zig** (`zig.exe`, `zig/lib/`, `zig/rt/`) | 0.17.0 | **MIT** | https://ziglang.org/ | Bundled C compiler (`zig cc`) and linker (`zig lld-link`) used on the user's machine to compile generated C modules into `yw1game_<build16>.dll`. |
| **Capstone** (inside `yw1gen`) | 5.0.9 | **BSD-3-Clause** | https://pypi.org/project/capstone/5.0.9/ | AArch64 disassembler used strictly inside the standalone `yw1gen.exe` subprocess. |
| **Unicorn Engine** (inside `yw1gen`) | 2.1.4 | **GPL-2.0** (Python bindings: BSD) | https://github.com/unicorn-engine/unicorn | Used strictly inside the standalone `yw1gen.exe` subprocess for bounds-checked jump-table analysis. |
| **python-lz4 / LZ4** (inside `yw1gen`) | 4.4.5 | **BSD-3-Clause** / **BSD-2-Clause** | https://pypi.org/project/lz4/4.4.5/ | NSO segment decompression inside `yw1gen.exe`. |
| **PyInstaller** (packs `yw1gen`) | 6.22.3 | **GPL-2.0+ with bootloader exception** | https://github.com/pyinstaller/pyinstaller | Standalone executable packaging for `yw1gen.exe`. |
| **CPython** (inside `yw1gen`) | 3.12.x | **PSF License** | https://github.com/python/cpython | Python runtime bundled inside `yw1gen.exe`. |

---

## Bundled Fonts

| Component | Version | License | Source / Repository | Notes |
|---|---|---|---|---|
| **Baloo 2 SemiBold** | 1.640 | **SIL OFL 1.1** | https://github.com/EkType/Baloo2 | Bundled UI font + `Baloo2-OFL.txt`. |
| **DejaVu Sans / Bold** | 2.37 | **Bitstream Vera / Public Domain** | https://github.com/dejavu-fonts/dejavu-fonts | Bundled fallback font + `DejaVu-LICENSE.txt`. |
| **BIZ UDPGothic Regular** | 1.05 | **SIL OFL 1.1** | https://github.com/googlefonts/morisawa-biz-ud-gothic | Bundled UI font resource (`third_party/fonts/BIZUDPGothic/`). |

*Note*: On Windows, the UI font loader also queries standard installed Windows CJK fonts (`Microsoft YaHei UI`, `Microsoft JhengHei UI`, `Yu Gothic UI`, `Malgun Gothic`) for complete Chinese/Japanese/Korean glyph coverage.

---

## External User Extraction Tools (Not Bundled)

`YokaiWatch1Recomp` does **not** ship `.nsp` / `.xci` extractors or .NET runtimes. Users supply already-extracted `exefs/` and `romfs/` folders dumped from their own legal copies using external open-source tools such as [`nxdumptool`](https://github.com/DarkMatterCore/nxdumptool), [`nstool`](https://github.com/jakcron/nstool), or [`LibHac` / `hactoolnet`](https://github.com/Thealexbarney/LibHac) (`Copyright © 2018-2022 Alex Barney`, MIT License).
