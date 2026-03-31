# 太空战机 / Space Fighter

**一个运行在 UEFI Shell 环境下的纵版射击游戏（Shmup）**

[English](#english) | 中文

---

## 游戏简介

太空战机是一款经典纵版射击游戏，完全运行在 UEFI Shell 环境中，不依赖任何操作系统。游戏通过 UEFI GOP（Graphics Output Protocol）直接输出 800x600 32位真彩色画面，使用 UEFI Simple Text Input Protocol 读取键盘输入。

玩家操控一架 F-22 风格的先进战斗机，在星空中迎击一波波涌来的敌机编队。击毁敌人获取分数和道具，升级武器火力，召唤僚机编队协同作战，击败强力 Boss。

![](docs/screenshot_title.png)
![](docs/screenshot_gameplay.png)

## 特色功能

- **纯 UEFI 环境运行**：不需要操作系统，在 BIOS/UEFI Shell 中直接运行
- **完整游戏循环**：标题画面 → 游戏进行 → 游戏结束，支持重新开始
- **5级武器升级系统**：从单发直射到五路满火力弹幕覆盖
- **僚机编队系统**：武器等级 2 起解锁双僚机，自动跟随并独立射击
- **5种道具**：火力提升、护盾、炸弹、生命、回复
- **4种敌机**：锋刃拦截机（小型）、打击轰炸机（中型）、装甲炮艇（大型）、无畏战舰（Boss）
- **炸弹清屏**：全屏清弹 + 全屏伤害 + 冲击波视觉特效
- **护盾系统**：限时免疫一切伤害并吸收敌弹
- **精细像素绘制**：所有战机使用多层 FillRect 手绘，含渐变、高光、面板线和动态动画
- **中文 HUD**：使用 16x16 像素点阵中文字体渲染界面文字

## 快速开始

### 环境要求

| 项目 | 要求 |
|------|------|
| 编译器 | Visual Studio 2019 |
| 汇编器 | NASM |
| EDK2 | [edk2](https://github.com/tianocore/edk2) 仓库 |
| Python | 3.x（EDK2 BaseTools 依赖） |
| 运行环境 | UEFI Shell 或 EDK2 EmulatorPkg 模拟器 |

### 编译

1. 克隆 EDK2 并初始化子模块：

```batch
git clone https://github.com/tianocore/edk2.git
cd edk2
git submodule update --init
```

2. 将本项目复制到 EDK2 目录结构中：

```batch
xcopy /E /I Shmup edk2\EmulatorPkg\Application\Shmup
```

3. 注册模块到 EDK2 构建系统：

在 `EmulatorPkg/EmulatorPkg.dsc` 的 `[Components]` 节添加：
```
EmulatorPkg/Application/Shmup/Shmup.inf
```

在 `EmulatorPkg/EmulatorPkg.fdf` 的 `[FV.FvRecovery]` 节添加：
```
INF EmulatorPkg/Application/Shmup/Shmup.inf
```

4. 编译：

```batch
edksetup.bat
set NASM_PREFIX=C:\nasm\
build -p EmulatorPkg\EmulatorPkg.dsc -a X64 -t VS2019 -b DEBUG
```

### 运行

**模拟器方式（推荐调试）：**

```batch
cd Build\EmulatorX64\DEBUG_VS2019\X64
echo Shmup.efi > startup.nsh
WinHost.exe
```

**真机 UEFI Shell：**

将生成的 `Shmup.efi` 复制到 FAT32 分区，在 UEFI Shell 中执行：

```batch
fs0:\Shmup.efi
```

## 操作方式

| 按键 | 功能 |
|------|------|
| ↑ ↓ ← → | 移动战机 |
| Space | 射击（按住连射） |
| X | 释放炸弹 |
| ESC | 退出游戏 |
| Enter | 开始 / 重新开始 |

## 项目结构

```
Shmup/
├── main.c        # 入口函数、定时器回调、主循环
├── game.c        # 游戏逻辑：生成、更新、碰撞检测、道具系统
├── render.c      # 渲染：所有精灵绘制、弹幕特效、HUD、画面呈现
├── input.c       # 输入处理：键盘读取、射击逻辑、僚机开火
├── types.h       # 类型定义、常量、结构体、函数声明
├── cn_font.h     # 16x16 中文点阵字库
└── Shmup.inf     # EDK2 模块构建描述文件
```

## 技术要点

**双缓冲渲染**：所有绘制操作先写入内存后备缓冲区，再通过 `EFI_GRAPHICS_OUTPUT_PROTOCOL.Blt()` 一次性提交到屏幕，避免画面撕裂。

**BGR 颜色格式**：UEFI GOP 使用蓝-绿-红顺序（`0x00BBGGRR`），非标准 RGB。

**无标准 C 运行时**：全部使用 EDK2 提供的库函数（`AllocatePool`/`FreePool`/`SetMem` 等），不依赖 `malloc`/`printf`。

**手绘像素精灵**：所有战机、敌机、子弹均使用 `FillRect` 和渐变辅助函数逐像素绘制，无外部图片资源。

**帧率控制**：通过 UEFI Timer Event 设置 8ms 周期定时器，实现约 125fps 的游戏帧率。

## 画面特效

- 玩家战机：F-22 造型，9层绘制（引擎喷焰 → 三角翼 → 鸭翼 → 武器挂架 → 机身 → 座舱 → 尾翼 → 喷管 → 开火闪光）
- 僚机：小型蓝色战斗机，带独立引擎尾焰和翼尖导航灯
- 敌机：4种类型，各有多层装甲、武器炮塔、动态引擎和威胁眼部设计
- 三种弹道视觉：主机蓝白弹 / 僚机青绿弹 / 敌方红色能量弹
- 爆炸粒子系统：多色粒子扩散，Boss 击毁特大爆炸
- 炸弹冲击波：白光闪烁 + 十字扩展环 + 四角爆裂
- 星空背景：多层视差滚动

## 许可证

MIT License

---

<a id="english"></a>

# Space Fighter

**A vertical scrolling shoot-em-up (Shmup) game running in UEFI Shell**

中文 | [English](#)

## Overview

Space Fighter is a classic vertical scrolling shoot-em-up game that runs entirely in the UEFI Shell environment, requiring no operating system. It renders 800x600 32-bit true-color graphics via the UEFI GOP (Graphics Output Protocol) and reads keyboard input through the UEFI Simple Text Input Protocol.

The player controls an F-22 style advanced fighter jet, battling waves of enemy squadrons in deep space. Destroy enemies for score and power-ups, upgrade weapons, summon wingmen for coordinated attacks, and defeat powerful bosses.

## Features

- **Runs in pure UEFI** — No OS required, executes directly in BIOS/UEFI Shell
- **Complete game loop** — Title screen → Gameplay → Game Over, with restart support
- **5-level weapon upgrade system** — From single shot to 5-way spread barrage
- **Wingman system** — Two AI wingmen unlock at weapon level 2, auto-follow and fire independently
- **5 power-up types** — Weapon, Shield, Bomb, Extra Life, Heal
- **4 enemy types** — Razor Interceptor (small), Strike Bomber (medium), Armored Gunship (large), Dreadnought (Boss)
- **Screen-clearing bomb** — Destroys all enemy bullets + damages all enemies + shockwave visual effect
- **Shield system** — Temporary invincibility that absorbs enemy fire
- **Detailed pixel art** — All sprites hand-drawn with layered FillRect, gradients, highlights, panel lines and animations
- **Chinese HUD** — Interface text rendered with 16x16 pixel Chinese bitmap font

## Quick Start

### Prerequisites

| Item | Requirement |
|------|-------------|
| Compiler | Visual Studio 2019 |
| Assembler | NASM |
| EDK2 | [edk2](https://github.com/tianocore/edk2) repository |
| Python | 3.x (required by EDK2 BaseTools) |
| Runtime | UEFI Shell or EDK2 EmulatorPkg |

### Build

1. Clone and initialize EDK2:

```batch
git clone https://github.com/tianocore/edk2.git
cd edk2
git submodule update --init
```

2. Copy this project into the EDK2 directory structure:

```batch
xcopy /E /I Shmup edk2\EmulatorPkg\Application\Shmup
```

3. Register the module in the EDK2 build system:

Add to `[Components]` section in `EmulatorPkg/EmulatorPkg.dsc`:
```
EmulatorPkg/Application/Shmup/Shmup.inf
```

Add to `[FV.FvRecovery]` section in `EmulatorPkg/EmulatorPkg.fdf`:
```
INF EmulatorPkg/Application/Shmup/Shmup.inf
```

4. Build:

```batch
edksetup.bat
set NASM_PREFIX=C:\nasm\
build -p EmulatorPkg\EmulatorPkg.dsc -a X64 -t VS2019 -b DEBUG
```

### Run

**Emulator (recommended for debugging):**

```batch
cd Build\EmulatorX64\DEBUG_VS2019\X64
echo Shmup.efi > startup.nsh
WinHost.exe
```

**Real hardware UEFI Shell:**

Copy `Shmup.efi` to a FAT32 partition, then in UEFI Shell:

```batch
fs0:\Shmup.efi
```

## Controls

| Key | Action |
|-----|--------|
| Arrow Keys | Move fighter |
| Space | Fire (hold for continuous) |
| X | Deploy bomb |
| ESC | Quit game |
| Enter | Start / Restart |

## Project Structure

```
Shmup/
├── main.c        # Entry point, timer callback, main loop
├── game.c        # Game logic: spawning, updating, collision, power-up system
├── render.c      # Rendering: all sprite drawing, bullet effects, HUD, presentation
├── input.c       # Input handling: keyboard reading, firing logic, wingman fire
├── types.h       # Type definitions, constants, structs, function declarations
├── cn_font.h     # 16x16 Chinese bitmap font data
└── Shmup.inf     # EDK2 module build description file
```

## Technical Highlights

**Double buffering** — All drawing writes to a memory back buffer first, then submits to screen via `EFI_GRAPHICS_OUTPUT_PROTOCOL.Blt()` in a single call to prevent tearing.

**BGR color format** — UEFI GOP uses Blue-Green-Red order (`0x00BBGGRR`), not standard RGB.

**No C runtime** — Uses only EDK2 library functions (`AllocatePool`/`FreePool`/`SetMem` etc.), no `malloc`/`printf`.

**Hand-drawn pixel sprites** — All fighters, enemies and bullets are drawn pixel-by-pixel using `FillRect` and gradient helpers. No external image assets.

**Frame rate control** — 8ms period UEFI Timer Event achieves approximately 125fps.

## Visual Effects

- Player ship: F-22 style, 9-layer rendering (engine exhaust → delta wings → canards → weapon pods → fuselage → cockpit → tail stabilizers → nozzles → muzzle flash)
- Wingmen: Small blue fighters with independent engine flames and navigation lights
- Enemies: 4 types with layered armor, weapon turrets, animated engines and menacing eye designs
- Three distinct bullet styles: Player blue-white / Wingman teal-green / Enemy red energy orbs
- Explosion particle system with multi-color particle bursts; Boss death triggers massive explosions
- Bomb shockwave: White flash + expanding cross ring + corner burst effects
- Parallax star field background

## License

MIT License
