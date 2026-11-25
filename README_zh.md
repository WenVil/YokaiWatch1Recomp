[English](README.md) | **简体中文**

# 13SentinelsRecomp

《十三机兵防卫圈 / 13 Sentinels: Aegis Rim》（Nintendo Switch 版）PC 原生静态重编译运行器，基于 [**peiikai/LASRecomp**](https://github.com/peiikai/LASRecomp) 构建。

<a href="https://recomplauncher.com"><picture><source media="(prefers-color-scheme: dark)" srcset="https://recomplauncher.com/assets/press/launcher/badge-light.svg"><img alt="Get it on RecompLauncher" src="https://recomplauncher.com/assets/press/launcher/badge-dark.svg" height="48"></picture></a>

---

## 版权与合规声明

> [!IMPORTANT]
> - **零内置版权资产**：本仓库及所有发行压缩包**不包含**任何《十三机兵防卫圈》游戏镜像、NSO 二进制代码、RomFS 资源、游戏字体、主机固件或解密密钥（`prod.keys`）。
> - **非模拟器**：本项目通过本机离线工具链，将玩家自己合法提取的 Switch 版游戏代码直接静态重编译为原生 x86-64 动态库（`lasgame_<build16>.dll`），配合 Vulkan 1.3 渲染后端与 HLE 系统服务在 Windows PC 上原生运行。
> - 本项目**不提供**任何获取游戏 ROM 或解密密钥（`prod.keys`）的途径、链接或工具。

---

## 游戏版本支持矩阵

| 地区与版本 | Title ID | Main Build ID（前 16 位） | 支持状态 | 内置游戏文本语言 |
|---|---|---|---|---|
| **亚洲版 v1.00** | `01008D7016438000` | `311947020F64F574` | **✅ 已实测支持** | 简体中文、繁體中文、한국어 |
| **欧美版 v1.10** | `01003FC01670C000` | `4B632573CA5C160E` | **✅ 已实测支持** | English, Français, Deutsch, Italiano, Español |
| **欧美版 v1.00** | `01003FC01670C000` | `57212E16CB83BB55` | **⚠️ 已识别（建议更新至 v1.10）** | English, Français, Deutsch, Italiano, Español |
| **日版 (JP)** | — | — | **⏳ 暂未适配验证** | — |

---

## 主要特性

- **原生 60 FPS 与 Vulkan 1.3 渲染**：实时稳定 60 FPS，支持窗口、无边框全屏与独占全屏（`window_mode`），多档垂直同步（`mailbox` / `fifo` / `immediate`）与着色器预热缓存。
- **超采样分辨率与画质增强选项**：支持内部渲染倍率（`auto` / `match` / `dynamic` / `25%` / `50%` / `75%` / `1x` / `1.5x` / `2x`）、输出分辨率（`auto` / `720p` / `1080p` / `1440p` / `4k`）、FSR 1 锐化放大（`fsr1` / `bilinear` / `auto`）与 SMAA 抗锯齿（`off` / `smaa`）。
- **五国语言前端与游戏内实时菜单**：启动前端与游戏内设置菜单（`F1` 或手柄 `Minus + Plus`）均支持 **简体中文 / 繁體中文 / English / 日本語 / 한국어** 高 DPI 无缺字渲染，并支持切换游戏内置文本语言（写入 `13sentinels.ini`）。
- **全流程手柄操控与键鼠改键**：内置 Nintendo Switch 与 Xbox 按键布局预设（`nintendo` / `xbox`），启动器与设置界面可全程手柄浏览操作，同时支持键鼠与手柄全按键自定义映射。
- **零版权本机自构建与存档管理**：首次运行自动并发生成并编译 `main`、`sdk`、`subsdk0` 三个模块为 `lasgame_<build16>.dll`（支持增量缓存续编与旧版 `larecomp.ini` 自动兼容迁移），并提供存档目录快捷打开与一键备份（`save_backups/`）。

---

## 系统要求

- **操作系统**：Windows 10 / 11（64-bit）
- **处理器**：支持 **AVX2** 与 **FMA** 指令集的 x86-64 处理器（`x86-64-v3`）
- **显卡**：支持 **Vulkan 1.3** 并已安装最新驱动的独立显卡或核显
- **内存 / 磁盘**：建议 16 GB RAM（最低 8 GB）；首次本机重编译需预留约 4~8 GB 可用磁盘空间

---

## 快速上手

### 第 1 步：下载并完整解压发行包

从 GitHub Releases 下载 `13SentinelsRecomp-*-windows.zip`。

> [!WARNING]
> **必须先将整个 `.zip` 压缩包完整解压到可写的本地文件夹中**（例如 `D:\Games\13SentinelsRecomp\`），再进入解压后的文件夹双击 **`13SentinelsRecomp.exe`** 启动。  
> **切勿在压缩包预览窗口中直接双击 `.exe`**——否则程序无法找到同目录的运行库 DLL、Zig 编译器与 `lasgen` 代码生成器工具链，导致无法启动或构建失败。

### 第 2 步：准备合法提取的 `exefs` 与 `romfs` 文件夹

从您自己合法持有的 Switch 版《十三机兵防卫圈》中提取游戏文件，使外层目录同时包含 `exefs/` 与 `romfs/`。（为避免要求玩家在本机安装 .NET 9.0 Runtime，标准发行包暂不内置 `.nsp`/`.xci` 解包器，请在前端直接选择已解包的文件夹。）

玩家从自己正版游戏提取 `exefs` 与 `romfs` 的 3 种常见开源方式：

1. **在 Switch 主机端直接导出（`nxdumptool`）**：在您的 Switch 上运行开源工具 `nxdumptool`，直接将卡带或已安装数字版游戏的 **ExeFS** 与 **RomFS** 导出至 SD 卡后拷贝到电脑。
2. **在 PC 端通过开源 Switch 模拟器提取（如 Ryujinx）**：使用开源模拟器 Ryujinx 加载您自己导出的游戏文件——**若为欧美版，请先在模拟器中挂载/启用 v1.10 更新补丁**——随后在游戏列表右键分别执行提取 **ExeFS** 与 **RomFS**，保存为 `exefs/` 与 `romfs/`。
3. **命令行工具解包（`hactoolnet` / `nstool`）**：使用 `hactoolnet`（LibHac）或 `nstool` 配合您从自己主机导出的 `prod.keys` 提取 ExeFS 与 RomFS 内容。

> [!NOTE]
> **欧美版 v1.10 合并说明**：使用命令行或补丁包提取欧美版 v1.10 时，务必将**基础游戏本体与 v1.10 更新补丁合并解包**，确保 `exefs/` 内为更新后的 v1.10 `main` 文件（Build ID 前 16 位为 `4B632573CA5C160E`），且 `romfs/` 内包含 `ROBO_PATCH_US.CPK`。

最终文件夹目录结构如下：

```text
13Sentinels_GameFiles/
├── exefs/
│   ├── main
│   ├── main.npdm
│   ├── rtld
│   ├── sdk
│   └── subsdk0
└── romfs/
    └── ROBO_*.CPK   （亚洲版 v1.00 含 ROBO_CN.CPK、ROBO_KO.CPK、ROBO_TW.CPK；
                      欧美版 v1.10 含 ROBO_US.CPK、ROBO_PATCH_US.CPK、ROBO_FR.CPK、ROBO_GE.CPK、ROBO_IT.CPK、ROBO_SP.CPK）
```

### 第 3 步：启动前端并执行本机编译

进入解压后的程序目录运行 **`13SentinelsRecomp.exe`**，在首页选择刚才准备好的 `13Sentinels_GameFiles/` 文件夹；确认区域与版本校验通过后，点击 **「本地构建游戏模块」** 或 **「开始游戏」**。首次全量生成并编译三模块通常需要约 **6~7 分钟**，此后启动会直接复用缓存秒进。

### 第 4 步：开始游玩与按键设置

游戏中可随时按 **`F1`** 或手柄 **`Minus + Plus`** 呼出游戏内设置菜单，调节分辨率、画质、音量、语言与键鼠/手柄按键映射；所有设置自动保存在程序同目录的 `13sentinels.ini` 中。

---

## 命令行与启动器集成参数（`RecompLauncher` 兼容）

支持以下标准 CLI 参数用于批处理脚本或第三方启动器集成（规范见 [recomplauncher.json](recomplauncher.json)）：

| 参数 | 说明 |
|---|---|
| `--game-dir <path>` | 指定包含 `exefs/` 与 `romfs/` 的已解包游戏根目录。 |
| `--build-only` | 仅执行无界面本机重编译生成 `lasgame_<build16>.dll` 并在完成后退出；向 `stdout` 输出去重后的 `progress <0.000~1.000> <stage>` 进度行。 |
| `--play` | 跳过启动器界面，直接使用当前已配置或 `--game-dir` 传入的目录启动游戏。 |

---

## 从源码构建（Windows MSYS2 UCRT64）

请先安装 MSYS2 UCRT64 编译依赖（详见 [docs/BUILD-windows.md](docs/BUILD-windows.md)），再准备 LGPL FFmpeg、Zig 与冻结版代码生成器：

```bash
bash tools/build_deps_windows.sh
bash tools/fetch_zig.sh
make -j8 GEN_PY=/path/to/python.exe gen-helper
make -j8 dist
```

生成的分发压缩包位于 `build/dist/13SentinelsRecomp-v1.0.0-beta1-windows.zip`。

---

## 致谢与开发声明

- **上游致谢**：本项目核心静态重编译运行器、AArch64 指令提升/转译框架、HLE 系统服务与 NVN-to-Vulkan 后端源自 [**peiikai/LASRecomp**](https://github.com/peiikai/LASRecomp)（MIT License）。  
  **请勿向 `peiikai/LASRecomp` 上游仓库反馈本项目的任何问题或提交 Issue！** 如遇《十三机兵防卫圈》适配、编译或运行问题，请仅在本项目仓库提交反馈。
- **开发方式透明声明**：本项目面向《十三机兵防卫圈》的适配分析、多语言 UI 改造、三模块自构建闭环、跳转表加固及全流程验证由**人类主导架构决策、测试设计与验收**，并**结合 AI 辅助编码开发完成**。
- **开源与第三方许可**：详见 [LICENSE](LICENSE) 与 [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)。
