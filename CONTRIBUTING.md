# Contributing to YokaiWatch1Recomp

Thank you for your interest in contributing to **YokaiWatch1Recomp**.

---

## Reporting Issues

- **Report issues here, not upstream**: Although `YokaiWatch1Recomp` is derived from `peiikai/LASRecomp`, please **do not** file issues or ask for support on `peiikai/LASRecomp`. Report all *Yo-kai Watch 1* compatibility, OpenGL rendering, build, or runtime bugs solely in this repository.
- When filing a bug report, please include:
  - Your Windows version, CPU model, GPU model, and OpenGL graphics driver version.
  - Your game version / Title ID (`0100C0000CEEA000`) and Main Build ID (`B6E172353E696E65`, etc.), plus any active mods under `mods/`.
  - A description of the issue and steps to reproduce.
  - The runtime log file (`yokaiwatch1recomp.log` next to `YokaiWatch1Recomp.exe`) and whether custom settings in `yokaiwatch1.ini` were used.

---

## Zero Copyright Policy (Strict)

- **Never commit, attach, or link copyrighted game assets.** This includes NSP/XCI dumps, decrypted Switch NSO executables (`main`, `rtld`, `sdk`, `subsdk*`), RomFS `.fa` archives, extracted game textures/audio/fonts, firmware files, save files, or `prod.keys`.
- Any test fixtures, scripts, or screenshots added to commits must be synthetically generated or purely runtime telemetry without proprietary game data.
- Keep `generated/` and `build/` out of version control (`.gitignore`).

---

## Code & Documentation Guidelines

- **0 GUARD Principle**: Never hand-edit outputs under `generated/`. All lifter fixes belong in `tools/gen.py` / `tools/lifter.py` / `tools/simd_c.py` and all HLE/GL/runtime behavior belongs in `runtime/`.
- **Clean Runtime Separation**: Keep changes focused, readable, and modular; run unit tests (`tools/tests` and `Makefile` test targets) and verify `llvm-nm` detects zero `f_70*` symbols in `YokaiWatch1Recomp.exe`.
- Update `README.md`, `README_zh.md`, `recomplauncher.json`, or `docs/` whenever CLI flags, `yokaiwatch1.ini` keys, or build steps change.

---

## Pull Request Process

1. Fork the repository and branch from `main`.
2. Ensure clean builds under Windows MSYS2 UCRT64 (`clang` + pure LGPL FFmpeg deps) with zero compiler warnings in touched files.
3. Describe what changed, how it was tested, and confirm zero copyrighted material or `generated/` edits are included.
