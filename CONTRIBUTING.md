# Contributing to 13SentinelsRecomp

Thank you for your interest in contributing to **13SentinelsRecomp**.

---

## Reporting Issues

- **Report issues here, not upstream**: Although 13SentinelsRecomp is derived from `peiikai/LASRecomp`, please **do not** file issues or ask for support on `peiikai/LASRecomp`. Report all *13 Sentinels: Aegis Rim* compatibility, build, or runtime bugs in this repository.
- When opening an issue, please include:
  - Your OS version, CPU model, GPU model, and Vulkan graphics driver version.
  - Game region and version (`Asia v1.00`, `US/EU v1.10`, or `US/EU v1.00`).
  - Clear steps to reproduce the problem.
  - The runtime log file (`13sentinelsrecomp.log` located next to `13SentinelsRecomp.exe`).

---

## Zero-Copyright & Legal Policy

All contributions MUST follow the project's strict zero-copyright rules:

- **Do NOT commit or upload** any Nintendo Switch game images (`.nsp`, `.xci`), NSO binaries (`main`, `sdk`, `subsdk0`, `rtld`), RomFS assets (`*.CPK`), game fonts, console firmware, decryption keys (`prod.keys`), or lifted/generated C source under `generated/`.
- **Do NOT include** links, instructions, or utilities for acquiring copyrighted game files or `prod.keys`.
- Recompilation tooling and runtime fixes must operate cleanly on user-supplied extracted `exefs/` and `romfs/` directories without bundling proprietary game data.

---

## Development Setup & Testing

- Supported primary build environment: **Windows 10/11 (x86-64) under MSYS2 UCRT64** — see [docs/BUILD-windows.md](docs/BUILD-windows.md).
- Ensure Git preserves LF line endings (`core.autocrlf false`, `core.eol lf`) so shell build scripts under `tools/` work reliably in MSYS2.
- Before submitting a pull request:
  - Keep changes focused and modular, preserving existing code style and comments.
  - Verify your build passes (`make -j8` or `make -j8 dist`) without compiler warnings in modified runtime code.
  - Run relevant unit/regression tests under `tools/` (for example Python lifting/cache tests or runtime C tests) matching the subsystems you touched.
  - Update `README.md`, `README_zh.md`, `recomplauncher.json`, or `docs/` if CLI flags, configuration keys (`13sentinels.ini`), or build steps change.

---

## Pull Request Process

1. Fork the repository and branch from `main`.
2. Make focused commits with clear descriptions of the motivation and testing performed.
3. Confirm `git status` contains zero copyrighted assets, personal machine paths, or build artifacts (`build/`, `generated/`).
4. Open a pull request against `main`.
