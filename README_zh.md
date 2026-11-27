[English](README.md) | **简体中文**

# YokaiWatch1Recomp

《妖怪手表 1 / Yo-kai Watch 1》（Nintendo Switch 版）Windows PC 原生静态重编译运行器，基于 [**peiikai/LASRecomp**](https://github.com/peiikai/LASRecomp) 构建。

<a href="https://recomplauncher.com"><picture><source media="(prefers-color-scheme: dark)" srcset="https://recomplauncher.com/assets/press/launcher/badge-light.svg"><img alt="Get it on RecompLauncher" src="https://recomplauncher.com/assets/press/launcher/badge-dark.svg" height="48"></picture></a>

---

## 版权与合规声明

> [!IMPORTANT]
> - **零内置版权资产**：本仓库及所有发行压缩包**不包含**任何《妖怪手表 1》游戏镜像、NSO 二进制文件、RomFS（`.fa` 等资源包）、游戏字体、主机固件、存档或解密密钥（`prod.keys`）。
> - **非模拟器**：本项目通过离线本机工具链（`yw1gen` + `zig`），将您自己合法提取的 Switch 游戏可执行模块（`exefs/main` 与 `exefs/sdk`）静态重编译为原生 x86-64 动态库（`yw1game_<build16>.dll`），配合 OpenGL 4.5 渲染后端与 HLE 系统服务在 Windows PC 上原生运行。
> - **纯 LGPL / MIT 运行库**：发行包内 FFmpeg 采用纯 LGPLv2.1+ 编译配置（`--disable-gpl`，零 `x264` / `x265` 等 GPL 编解码器），搭配 zlib 授权的 SDL3 与 MIT/BSD 运行时组件。
> - 本项目**不提供**任何获取盗版 ROM 或密钥（`prod.keys`）的途径、链接或工具。

---

## 支持的游戏版本

| 游戏版本与地区 | Title ID | Main Build ID（前 16 位） | 支持状态 | 内置文本 / Mod 支持 |
|---|---|---|---|---|
| **妖怪手表 1（Nintendo Switch 日版 v1.3.0）** | `0100C0000CEEA000` | `B6E172353E696E65` | **✅ 已实测验证** | 遵循游戏原始数据语言（默认 `game_lang = auto`）+ `mods/` LayeredFS 覆盖层 |
| **其他版本（`v1.0.0`、`v1.4.0+` 等）** | `0100C0000CEEA000` | 运行时特征自动定位 | **✅ 全版本通配地址解析** | 基于特征扫描自动定位符号偏移，适配跨版本构建 |

---

## 主要特性

- **OpenGL 4.5 硬件渲染、独显首选与高分辨率超采样**：使用虚拟 `1920x1080` 帧缓冲并支持内部分辨率超采样（`res_scale`：`1x` 1080p / `1.5x` 1620p / `2x` 4K 2160p），导出 `NvOptimusEnablement` / `AmdPowerXpressRequestHighPerformance` 自动优先调度独立显卡，支持窗口 / 无边框全屏 / 独占全屏（`window_mode`）、`16:9` 等比或铺满拉伸（`aspect_mode`）及多档垂直同步（`mailbox` / `fifo` / `immediate`）。
- **原生 60 帧模式（带跟随镜头平滑修复）与实时倍速（`1.0x` ~ `3.0x`）**：针对 Switch 日版 `v1.3.0` 原生解锁 **60 FPS**（默认开启，可在启动器或 `F1` 显示设置中随时热切换回原版 `30 FPS`，`fps_mode`），并内置 3D 跟随相机平滑插值修复；未验证版本自动安全维持 `30 FPS`。支持按 **`F3`** 或设置随时切换 `1.0x` / `1.5x` / `2.0x` / `3.0x` 倍速（`game_speed`），可与 60 帧直接叠加并保持过场影片自动回归 `1.0x`。
- **LayeredFS Mod 覆盖层（`mods/`，重点实测支持英文补丁）**：可将第三方 Switch 民间翻译（如 [GameBanana 英文补丁 Release 1.2.0](https://gamebanana.com/mods/516048)）或汉化/改模包放入 `mods/<Mod名称>/romfs/`，通过 `mods_enabled = 1` 即插即用，增删或切换 Mod 无需重新编译游戏代码，也不改动原始解包文件。
- **五国语言高 DPI 启动器与 `F1` 实时覆盖菜单**：启动前端与游戏内设置菜单（`F1` 或手柄 `Minus + Plus`）均完整支持 **简体中文 / 繁體中文 / English / 日本語 / 한국어** 高 DPI 无缺字渲染（加载 Windows ClearType/DirectWrite 系统字体与开源 OFL 字体）。
- **手柄 / 键鼠 / 触摸全面适配**：提供 Nintendo Switch 物理键位与 Xbox 字母键位预设（注意：`Xbox` 仅重映射手柄输入布局，不会替换游戏内显示的 Switch 按键图标），支持全按键自定义、手柄震动强度调节（`rumble_strength`）以及鼠标点击直接映射触屏交互；设置保存在 `yokaiwatch1.ini`。
- **极速双模块自构建与存档备份**：首次运行通过自带的 `yw1gen.exe` 与精简版 `Zig 0.17.0` 编译器并发重编译 `main` + `sdk` 为 `yw1game_<build16>.dll`（中间产物缓存于 `.gamebuild/`），存档保存于 `save/` 并自动备份到 `save_backups/`。

---

## 系统要求

- **操作系统**：Windows 10 / 11（64-bit）
- **处理器**：支持 **AVX2** 与 **FMA** 指令集的 x86-64 CPU（`x86-64-v3`）
- **显卡**：支持 **OpenGL 4.5** 并已安装最新显卡驱动的独立显卡或核显
- **内存 / 磁盘**：建议 8 GB 及以上内存；预留约 1 GB 可用空间用于代码生成缓存与模块构建

---

## 快速上手

### 第 1 步：下载并完整解压发行包

从 [WenVil/YokaiWatch1Recomp Releases](https://github.com/WenVil/YokaiWatch1Recomp/releases) 页面（`https://github.com/WenVil/YokaiWatch1Recomp`）下载 **`YokaiWatch1Recomp-v1.0.0-beta1-windows.zip`**。

> [!WARNING]
> **务必先将整个 `.zip` 压缩包完整解压到可写的本地目录**（如 `D:\Games\YokaiWatch1Recomp\`），再进入解压目录双击 **`YokaiWatch1Recomp.exe`**。  
> **切勿直接在 Windows 压缩包预览界面中双击 `.exe`**——程序需要读取同目录下的 DLL、`zig/`、`helper/yw1gen/`、`gamelib-src/` 与字体资源。

### 第 2 步：准备合法提取的 `exefs` 与 `romfs` 文件夹

从您自己合法持有的 Switch 版《妖怪手表 1》（`0100C0000CEEA000`）中导出游戏数据（例如在 Switch 端使用开源工具 `nxdumptool`，或在 PC 上使用 `hactoolnet` / `nstool` 配合您自己主机的 `prod.keys` 解包）：

```text
YokaiWatch1_GameFiles/
├── exefs/
│   ├── main
│   ├── main.npdm
│   ├── rtld
│   └── sdk
└── romfs/
    └── yw1_a.fa   （以及同目录下的其他 romfs 资源）
```

*（可选）启用 Mod / 翻译补丁*：在程序目录（或自定义数据目录）下的 `mods/<Mod名称>/romfs/` 放入与原版路径一致的覆盖文件即可自动生效。

### 第 3 步：构建并运行

运行 **`YokaiWatch1Recomp.exe`**，选择 `YokaiWatch1_GameFiles/` 目录后点击 **「本地构建游戏模块」** 或 **「开始游戏」**。  
首次双模块编译完成后，后续启动将直接复用缓存 `yw1game_<build16>.dll` 秒进。

### 默认快捷键

| 快捷键 | 功能说明 | INI 配置项 |
|---|---|---|
| `F1`（或手柄 `Minus + Plus`） | 打开 / 关闭游戏内设置菜单 | `hotkey_menu` |
| `F2` | 切换性能监视浮层（关 / FPS / 详细） | `hotkey_hud` |
| `F3` | 循环切换游戏倍速（`1.0x` / `1.5x` / `2.0x` / `3.0x`） | `hotkey_speed` |
| `F11` | 切换窗口 / 全屏模式 | `hotkey_fullscreen` |

---

## Mod 与 LayeredFS 覆盖层使用说明

### 英文补丁（English Translation Mod）使用教程

> [!NOTE]
> **第三方社区作品声明**：下方提供的英文翻译补丁为第三方玩家社区独立制作与分发；`YokaiWatch1Recomp` 项目仓库与发行包均**不内置、不打包、不分发**任何第三方 Mod 或游戏版权资产。

- **第三方原帖下载地址**：[*YO-KAI WATCH 1 for Switch English Translation*（GameBanana）](https://gamebanana.com/mods/516048)（`https://gamebanana.com/mods/516048`），已在 Switch 日版 **`v1.3.0`** 下配合英文补丁 **Release 1.2.0** 实测通过。

#### 三步安装教程

1. 从上述 GameBanana 页面下载英文补丁压缩包并解压。
2. 在启动器中点击 **「打开 Mods 目录」（Open Mods Folder）**（或 RecompLauncher 的 `mods` 文件夹快捷入口），打开 `<data_root>/mods/` 目录。
3. 将解压得到的 `0100C0000CEEA000` 文件夹直接复制到 `mods/` 目录中（或新建 `mods/en_patch/` 并把 `romfs/` 放入其中）；确认启动器中 **「启用 mods/ 资源覆盖」（Enable mods/）** 已勾选并显示 **`Mods: 1 个已启用`（`Mods: 1 active`）** 后直接启动游戏（后续更换或增删 Mod **均无需重新编译**）。

#### 正确与错误目录结构对照

```text
# 正确目录结构：
<data_root>/mods/
├── en_patch/
│   └── romfs/...              （推荐：自定义 Mod 名称文件夹内直接放 romfs）
└── 0100C0000CEEA000/
    └── romfs/...              （也正确：直接放置解压出的 Title ID 文件夹）

# 错误目录结构（无法识别加载）：
<data_root>/mods/romfs/...                         （缺少 <Mod名称> 这一层级）
<data_root>/mods/en_patch/0100C0000CEEA000/romfs/  （多套了一层目录）
```

#### 中文与其它语言 / 改模 Mod 说明

同一套 `mods/` LayeredFS 覆盖机制同样支持其它第三方民间语言补丁（如中文补丁、西班牙语补丁等）以及贴图/模型替换等 Mod；当 `mods/` 下存在多个 Mod 文件夹时，系统会按文件夹名称的**字母顺序**依次叠加覆盖。

---

## 命令行参数与启动器集成（`RecompLauncher`）

项目包含通过校验的 [recomplauncher.json](recomplauncher.json) 配置清单，支持以下命令行与环境变量：

| 参数 / 环境变量 | 说明 |
|---|---|
| `--game-dir <path>` | 指定包含 `exefs/` 与 `romfs/` 的已解包游戏根目录。 |
| `--build-only` | 无界面仅编译 `yw1game_<build16>.dll` 并退出；在 `stdout` 输出 `progress <0.000~1.000> <stage>` 进度行。 |
| `--play` | 跳过启动器界面，直接使用指定或已配置的目录进入游戏。 |
| `RECOMPLAUNCHER_MODE` | 指定启动模式（`build` / `play` / `launcher`）。 |
| `RECOMPLAUNCHER_DATA` / `LARECOMP_DATA_ROOT` | 指定用户数据目录（包含 `yokaiwatch1.ini`、`save/`、`mods/`、`.gamebuild/`）。 |
| `RECOMPLAUNCHER_PAD` | 覆盖初始手柄映射预设（`nintendo` / `xbox` / `playstation`）。 |
| `YW1_EXP_60FPS` | 强制锁定帧率模式（`1` = 日版 `v1.3.0` 原生 60 FPS，`0` = 原版 30 FPS）；优先级高于 `yokaiwatch1.ini` 中的 `fps_mode`。 |

---

## 从源码构建（Windows MSYS2 UCRT64）

从源码编译及生成纯 LGPL 分发包（`make dist`）的详细步骤请参阅 [docs/BUILD-windows.md](docs/BUILD-windows.md)。

---

## 致谢与开发声明

- **上游致谢**：核心静态重编译引擎、AArch64 指令提升/转译器、HLE 系统服务与自构建缓存框架源自 [**peiikai/LASRecomp**](https://github.com/peiikai/LASRecomp)（MIT License）。  
  **请勿向 `peiikai/LASRecomp` 提交任何《妖怪手表 1》相关的 Issue 或 Pull Request！** 所有适配、渲染、构建或运行问题请仅在本仓库反馈。
- **开发方式透明声明**：《妖怪手表 1》逆向适配、OpenGL 4.5 兼容层实现、ASTC 软解解码、动态步长/倍速机制、LayeredFS Mod 挂载、纯 LGPL 依赖瘦身与自动化回归测试均由**人类主导架构决策、测试设计与验收，并结合 AI 辅助编码开发完成**。
- **开源许可**：详见 [LICENSE](LICENSE) 与 [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)。
