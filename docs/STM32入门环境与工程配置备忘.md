# STM32 入门环境与工程配置备忘

> **相关文档**  
> - [文件说明.md](./文件说明.md) — Embedded 目录索引  
> - [STM32最小系统板与面包板器件说明.md](./STM32最小系统板与面包板器件说明.md) — 蓝 pill 板载元器件  
> - [STM32F103C8T6引脚说明.md](./STM32F103C8T6引脚说明.md) — 排针丝印与芯片引脚  
> - [STM32短期入门规划.md](./STM32短期入门规划.md) — 2 周入门日程

更新时间：2026-07-12

---

## 一、一句话理解

本文汇总 **蓝 pill + Keil MDK + 标准外设库（SPL）** 入门过程中遇到的 **环境搭建、资料下载、工程配置与排错做法**，按「先做什么、怎么做」组织，便于对照视频教程自行操作。

**记忆口诀**：**Pack 认芯片，SPL 给库函数，RM0008 查寄存器；V6 别编 core_cm3.c，Define 两个宏都要有。**

---

## 二、硬件与文档（已做事项）

### 2.1 蓝 pill 板子实拍图归档

| 行动 | 做法 |
|------|------|
| 保存图片 | 放入 `attachments/stm32-blue-pill-board.png` |
| 补充板载说明 | 在 [STM32最小系统板与面包板器件说明.md](./STM32最小系统板与面包板器件说明.md) §3.1 增加板载元器件对照表（AMS1117、BOOT0/BOOT1、SWD 四针口等） |
| 补充排针丝印 | 在 [STM32F103C8T6引脚说明.md](./STM32F103C8T6引脚说明.md) §2.1 增加蓝 pill 上下排针丝印顺序 |

**原则**：已有内容不重复写；只补图中出现、原文未单独说明的部分。

---

## 三、资料下载清单

### 3.1 Keil 设备支持包（DFP）

| 项 | 说明 |
|----|------|
| **包名** | `Keil::STM32F1xx_DFP`（如 `Keil.STM32F1xx_DFP.2.4.1.pack`） |
| **作用** | 让 Keil 识别 STM32F103C8、可选型号、配置调试 |
| **在线安装** | Keil → **Project → Manage → Pack Installer** → STMicroelectronics → STM32F1 Series → Install |
| **离线安装** | [Keil STM32F1xx_DFP 版本页](https://www.keil.arm.com/packs/stm32f1xx_dfp-keil/versions/) 下载 `.pack`，双击或 Pack Installer **File → Import** |
| **不需要** | F0/F4/L0/L4 等其他系列 Pack（除非换芯片） |

### 3.2 标准外设库（SPL）

| 项 | 说明 |
|----|------|
| **包名** | `STM32F10x_StdPeriph_Lib_V3.5.0` |
| **作用** | 提供 `GPIO_Init()`、`RCC_APB2PeriphClockCmd()` 等库函数源码 |
| **官方下载** | [STSW-STM32054](https://www.st.com/en/embedded-software/stsw-stm32054.html) |
| **常用拷贝路径** | `Libraries/STM32F10x_StdPeriph_Driver/src、inc` → 工程 Library；`CMSIS/.../startup/*.s` → Start；`stm32f10x.h` 等 → Start/User |

### 3.3 参考手册与数据手册

| 文档 | 用途 | 下载 |
|------|------|------|
| **RM0008** 参考手册 | RCC、GPIO、USART 等外设寄存器（教程里查 RCC_APB2ENR） | [ST 英文 PDF](https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) |
| **STM32F103x8** 数据手册 | 引脚、电气参数 | [Datasheet](https://www.st.com/resource/en/datasheet/stm32f103c8.pdf) |
| **中文版 RM0008** | 教程常用，社区翻译（基于旧版 Rev 10） | 教程资料包自带；与最新英文版有出入时 **以英文为准** |

**读手册示例（点灯）**：目录 → **RCC** → `RCC_APB2ENR` → 置位 **IOPCEN** 使能 GPIOC 时钟。

---

## 四、Keil 后台更新（cpacketget / Pack Installer）

### 4.1 cpacketget.exe 在做什么

- 路径示例：`Keil_v5\ARM\cmsis-toolbox\bin\cpacketget.exe`
- **作用**：后台检查并下载各厂商 CMSIS Pack（ST、NXP、TensorFlow 等）
- **与学 F103 关系**：可忽略大部分；**只需 STM32F1xx_DFP**

### 4.2 窗口显示「选择」、像卡住

| 现象 | 做法 |
|------|------|
| 标题栏出现 **「选择」** | 在黑色窗口按 **Enter** 或 **Esc** 解除暂停（误点鼠标常见） |
| 某 Pack 下载超时（如 TensorFlow） | 直接关掉窗口；不影响已装 Keil |
| 不想等全量更新 | Pack Installer 里 **只装 F1 DFP**，不要 Update All |

### 4.3 Pack Installer 报文件占用

```
The process cannot access the file because it is being used by another process
```

| 做法 |
|------|
| 关掉多余的 `cpacketget.exe` / Pack Installer 窗口 |
| 只保留一个 Pack Installer |
| 仍失败则重启 Keil，或改用 **离线 .pack** 导入 |

### 4.4 Pack Installer 跑完后的界面

- 左侧 **Devices**：厂商与芯片列表
- 右侧 **Packs**：`Up to date` / `Update` / `Install`
- 学 F103：**搜 STM32F1** → 装 `Keil::STM32F1xx_DFP` 即可

---

## 五、Keil 工程配置（test 项目实践）

工程路径示例：`Embedded/project/test.uvprojx`  
芯片：**STM32F103C8**（蓝 pill）

### 5.1 编译器：只有 V6、没有 V5

| 现象 | 原因 |
|------|------|
| `Default Compiler Version 5 which is not available` | 本机未安装 ARM Compiler 5（`ARM\ARMCC`） |

| 做法 | 说明 |
|------|------|
| **推荐** | **Options for Target → Target** → 选 **V6.24 (ARMCLANG)** |
| 备选 | 重装 MDK 并勾选 **ARM Compiler 5**（与多数旧教程一致） |
| **勿做** | 在无 V5 时强行选 Compiler 5 |

### 5.2 core_cm3.c 与 Compiler 6 不兼容

| 现象 | 原因 |
|------|------|
| `non-ASM statement in naked function is not supported`（4 个错误） | 旧版 SPL 自带的 `core_cm3.c`（CMSIS V1.30）按 V5/GCC 写法，与 ArmClang V6 规则冲突 |

| 做法 |
|------|
| 从工程 **移除 `core_cm3.c`**（Remove from Group，不删磁盘文件） |
| **保留 `core_cm3.h`** |
| 入门点灯 / 寄存器操作 **不需要** 编译 `core_cm3.c` |

### 5.3 Define 宏（C/C++ → Preprocessor Symbols）

两个宏 **都要**，英文逗号分隔：

```
STM32F10X_MD,USE_STDPERIPH_DRIVER
```

| 宏 | 作用 |
|----|------|
| `STM32F10X_MD` | F103C8 为 **中容量（Medium Density）** |
| `USE_STDPERIPH_DRIVER` | 启用标准外设库；`stm32f10x.h` 会包含 `stm32f10x_conf.h` 及各外设头文件 |

### 5.4 Include Path

教程用标准库时建议：

```
.\Start;.\Library;.\User
```

| 路径 | 内容 |
|------|------|
| `.\Start` | `stm32f10x.h`、`system_stm32f10x.c`、启动文件 |
| `.\Library` | `stm32f10x_gpio.h` 等库头文件 |
| `.\User` | `main.c`、`stm32f10x_conf.h` |

### 5.5 工程文件分组（建议）

| 分组 | 文件 |
|------|------|
| **Start** | `startup_stm32f10x_md.s`、`stm32f10x.h`、`system_stm32f10x.c`、`core_cm3.h`（**不要** `core_cm3.c`） |
| **Library** | 用到的 `.c`（如 `stm32f10x_gpio.c`、`stm32f10x_rcc.c`）；头文件通过 Include Path 引用 |
| **User** | `main.c`、`stm32f10x_conf.h`、`stm32f10x_it.c`（若教程有） |

### 5.6 直接写寄存器 vs 库函数

| 写法 | 是否需要 Library 里的 `.c` |
|------|---------------------------|
| `RCC->APB2ENR`、`GPIOC->CRH`（寄存器） | 暂不需要 |
| `RCC_APB2PeriphClockCmd()`、`GPIO_Init()`（库函数） | 需把对应 `stm32f10x_rcc.c`、`stm32f10x_gpio.c` 加入工程 |

---

## 六、推荐操作顺序（从零到首次编译通过）

```text
1. 装 Keil MDK
2. 装 STM32F1xx_DFP（Pack Installer 或离线 .pack）
3. 下 SPL 固件库，按教程拷贝 Start / Library / User
4. 新建工程，选 STM32F103C8
5. Options → Target：Compiler V6.24
6. Options → C/C++：Define 填 STM32F10X_MD,USE_STDPERIPH_DRIVER
7. Options → C/C++：Include Path 填 .\Start;.\Library;.\User
8. 工程里移除 core_cm3.c，保留 core_cm3.h
9. 加入 startup_stm32f10x_md.s、system_stm32f10x.c、main.c
10. Build → 0 Error 后再接 ST-Link 烧录
```

---

## 七、常见问题速查

| 现象 | 可能原因 | 处理 |
|------|----------|------|
| Compiler 5 not available | 未装 ARMCC | 改用 V6.24 |
| core_cm3.c 4 个 naked 错误 | V6 + 旧 CMSIS | 移除 `core_cm3.c` |
| cpacketget 不动 | 窗口被暂停 | Enter / Esc |
| Pack 文件占用 | 多进程同时更新 | 关多余窗口，离线导入 |
| 找不到 `GPIO_Init` | 未定义 `USE_STDPERIPH_DRIVER` 或未加 `.c` | 补宏、Include Path、Library 源文件 |
| 时钟/容量不对 | 缺 `STM32F10X_MD` | Define 里加上 |
| 教程要 V5、我只有 V6 | 环境差异 | V6 + 去 core_cm3.c 可继续学 |

---

## 八、DFP 与 SPL 的区别（易混）

| | Keil F1 DFP | SPL 标准外设库 |
|--|-------------|----------------|
| **是什么** | Keil 设备支持包 | ST 官方库函数源码 |
| **解决什么** | 选芯片、调试、Flash 算法 | `GPIO_Init` 等 API |
| **要不要装** | **要** | **要**（跟 SPL 教程时） |

---

## 九、与本项目文档的衔接

| 入门硬件 / 概念 | 本项目文档 |
|----------------|------------|
| 蓝 pill 引脚、接线 | [STM32F103C8T6引脚说明.md](./STM32F103C8T6引脚说明.md)、[STM32最小系统板与面包板器件说明.md](./STM32最小系统板与面包板器件说明.md) |
| GPIO / UART / I2C | [STM32外设说明.md](./STM32外设说明.md) |
| 总线、RCC 概念 | [STM32系统结构说明.md](./STM32系统结构说明.md) |
| 2 周练习节奏 | [STM32短期入门规划.md](./STM32短期入门规划.md) |
