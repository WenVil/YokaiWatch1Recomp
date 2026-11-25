# Third-Party Licenses & Source Attribution

`13SentinelsRecomp` does **not** include any game binaries, RomFS assets, firmware, or decryption keys. The runtime and build tools incorporate or dynamically load the third-party open-source components listed below.

---

## Core Runtime & UI Dependencies

| Component | Version | License | Source / Repository | Notes |
|---|---|---|---|---|
| **LASRecomp** (upstream base) | 2026 | **MIT** | https://github.com/peiikai/LASRecomp | AArch64 static recompilation runtime, HLE services, NVN/Vulkan translation, and build-cache framework (`Copyright (c) 2026 Peiikai`). |
| **SDL3** (`SDL3.dll`) | 3.4.x | **zlib License** | https://github.com/libsdl-org/SDL | Dynamically linked runtime package from MSYS2 UCRT64 (`https://packages.msys2.org`). |
| **Dear ImGui** | vendored (`third_party/imgui/`) | **MIT** | https://github.com/ocornut/imgui | Launcher UI and in-game `F1` / controller overlay. |
| **Vulkan Memory Allocator (VMA)** | MSYS2 `vulkan-memory-allocator` | **MIT** | https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator | GPU memory allocator compiled into `runtime/nvn/vulkan/vma.cpp`. |
| **shaderc** (`libshaderc_shared.dll`) | 2026.x | **Apache-2.0** | https://github.com/google/shaderc | Runtime GLSL-to-SPIR-V compiler dynamically linked from MSYS2 UCRT64 (`https://packages.msys2.org`). |
| **FFmpeg** (`avcodec-63.dll`, `avformat-63.dll`, `avutil-61.dll`, `swscale-10.dll`) | **9.0.2** | **LGPLv2.1+** | https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz | Unmodified official FFmpeg 9.0.2 source built in LGPL-only mode via MSYS2 UCRT64 (`tools/build_deps_windows.sh`, without `--enable-gpl` / `x264` / `x265` / `xvid` / `postproc`). |
| **SMAA** | 2013 master (`third_party/smaa/`) | **MIT** | https://github.com/iryoku/smaa | Reference anti-aliasing shader and lookup textures. |
| **Ryujinx ASTC Decoder** | `third_party/ryujinx/` | **MIT** | https://github.com/Ryujinx/Ryujinx | ASTC texture decoding helper (`Copyright (c) Ryujinx Team and Contributors`). |

---

## Bundled Local Self-Build Toolchain & Fonts (`dist/`)

When users compile their own legally dumped game files locally on first launch, the distribution invokes the following helper tools as separate processes:

| Component | Version | License | Source / Repository | Notes |
|---|---|---|---|---|
| **Zig** (`zig/` with LLVM / Clang / LLD) | **0.17.0** | **MIT** (Zig); **Apache-2.0 with LLVM exceptions** (LLVM/Clang/LLD) | https://ziglang.org/download/0.17.0/zig-0.17.0.tar.xz | Slimmed compiler toolchain used cleanly as a subprocess to compile generated C sources into `lasgame_<build16>.dll`. |
| **Capstone** (inside `lasgen`) | 5.0.9 | **BSD-3-Clause** | https://pypi.org/project/capstone/5.0.9/ | AArch64 disassembler used by the standalone `lasgen` subprocess. |
| **Unicorn Engine** (inside `lasgen`) | 2.1.4 | **GPL-2.0** (Python bindings: BSD) | https://github.com/unicorn-engine/unicorn | Used strictly inside the standalone `lasgen.exe` subprocess for bounds-checked jump-table analysis. |
| **python-lz4 / LZ4** (inside `lasgen`) | 4.4.5 | **BSD-3-Clause** / **BSD-2-Clause** | https://pypi.org/project/lz4/4.4.5/ | NSO segment decompression inside `lasgen.exe`. |
| **PyInstaller** (packs `lasgen`) | 6.22.3 | **GPL-2.0+ with bootloader exception** | https://github.com/pyinstaller/pyinstaller | Standalone executable wrapper for `lasgen`. |
| **CPython** (inside `lasgen`) | 3.12 | **PSF License** | https://github.com/python/cpython | Python runtime bundled in `lasgen`. |
| **libiconv** (`libiconv-1.19` / `libiconv-2.dll`) | 1.19 | **LGPLv2.1+** | https://ftp.gnu.org/pub/gnu/libiconv/ | Character set conversion DLL from MSYS2 UCRT64. |
| **GCC Runtime** (`libgcc_s_seh-1.dll`, `libstdc++-6.dll`) | MSYS2 UCRT64 | **GPL-3.0 with GCC Runtime Library Exception** | https://gcc.gnu.org/ | Standard runtime DLLs from MSYS2 UCRT64 packages. |
| **winpthreads** (`libwinpthread-1.dll`) | MSYS2 UCRT64 | **MIT / BSD** | https://www.mingw-w64.org/ | POSIX threading DLL from MSYS2 UCRT64 packages. |
| **Baloo 2 SemiBold** | 1.640 | **SIL OFL 1.1** | https://github.com/EkType/Baloo2 | Bundled font + `Baloo2-OFL.txt`. |
| **DejaVu Sans / Bold** | 2.37 | **Bitstream Vera / Public Domain** | https://github.com/dejavu-fonts/dejavu-fonts | Bundled fallback font + `DejaVu-LICENSE.txt`. |
| **BIZ UDPGothic Regular** | 1.05 | **SIL OFL 1.1** | https://github.com/googlefonts/morisawa-biz-ud-gothic | Bundled UI font resource (`third_party/fonts/BIZUDPGothic/`). |

---

## Optional External Package Extractor

| Component | Version | License | Source / Repository | Notes |
|---|---|---|---|---|
| **LibHac** (used if `nx_extract` is built/supplied separately) | **0.19.0** | **MIT** | https://github.com/Thealexbarney/LibHac | `Copyright © 2018-2022 Alex Barney`. The standalone distribution omits `nx_extract.exe` to avoid requiring the .NET 9.0 Runtime; users point the launcher directly at extracted `exefs` / `romfs` folders (or supply their own external `nx_extract.exe` + `prod.keys`). |
