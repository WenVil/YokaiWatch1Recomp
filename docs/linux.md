# Linux Build Notes (Unverified Upstream Base)

> [!WARNING]
> **Windows x86-64 is currently the only tested and supported release platform for `13SentinelsRecomp`.**  
> The Linux `Makefile` rules and container/AppImage scripts under `tools/linux/` are inherited from upstream `peiikai/LASRecomp` and have **not** been validated end-to-end with *13 Sentinels: Aegis Rim*. Notes below describe the upstream Linux build targets preserved in the repository.

---

## Target Architectures & Prerequisites

`Makefile` supports Linux builds for `x86_64` (requiring AVX2 + FMA via `-march=x86-64-v3`) and `aarch64`:

- **Compiler / Linker**: `clang`, `clang++`, `lld`
- **Libraries**: `SDL3`, `FFmpeg` (`libavformat`, `libavcodec`, `libavutil`, `libswscale`), `Vulkan` headers/loader, `shaderc` (`libshaderc` / `libshaderc_shared`), `pthread`, `dl`, `m`
- **Packaging**: `podman` (used by `tools/linux/appimage.sh` with `tools/linux/Containerfile`)

---

## Build Targets

### Native Development Build

```bash
make -j$(nproc)
```

Outputs `build/lasrecomp` (the Linux executable target in `Makefile`). You can override `SHADERC=...` if your distribution ships `libshaderc_shared` without a standard `shaderc.pc` pkg-config file, and set `GENARCH=` if cross-targeting CPUs without AVX2/FMA.

### Containerized AppImage Build

`tools/linux/appimage.sh` builds inside an Ubuntu 22.04 container via `podman`:

```bash
make appimage          # host architecture
make appimages         # both x86_64 and aarch64
```

System Vulkan loaders (`libvulkan.so.1`) and GPU drivers are intentionally left to the host distribution rather than bundled inside the AppImage.
