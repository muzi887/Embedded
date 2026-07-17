# Keil New Project 从零建（只复制代码）

更新时间：2026-07-17  
前提：目标目录树见 [从零重建江协风格工程指南.md](./从零重建江协风格工程指南.md)  
旧工程：`tikong/tikong`（只当源码库，**不要改、不要删**）  
新工程：例如 `tikong/tikong_jx`（名称自定）

> **你选的路径**：Keil → **New µVision Project**；只拷 `.c/.h/.s` 与 `Library`，**不拷**旧的 `Template.uvprojx`。

---

## 总顺序（照着做）

```text
1. 建空文件夹树
2. 只拷代码（按表）
3. Keil New Project → 存到 User/
4. 关掉向导多余组件，改用自己的 Start/Library
5. 配 Define + Include Path
6. 建分组，把 .c / .s 加进去
7. 选调试器 → Rebuild → 修错
```

---

## 第一步：空目录

在新工程根（如 `tikong_jx`）建好：

```text
User/
System/delay/
System/sys/
Hardware/Serial/
Hardware/Storage/
Hardware/Time/
Hardware/Board/
Hardware/Modem/
App/Pass/
App/Cloud/
App/Store/
App/Link/
Middlewares/
Library/
Start/
docs/          （可选）
```

---

## 第二步：只拷代码（不拷工程文件）

从旧 `tikong/tikong` **复制文件**到上表对应目录。  
**不要复制** `USER/Template.uvprojx`、`.uvoptx`、`OBJ/`。

按 [从零重建江协风格工程指南.md](./从零重建江协风格工程指南.md) **§五** 整表拷；最少要有：

| 拷到 | 内容 |
| --- | --- |
| `Start/` | 旧 `CORE/*` + 旧 `USER/system_stm32f4xx.c`、`USER/system_stm32f4xx.h` |
| `Library/` | 旧 `FWLIB/` 整份（保留 `inc/`、`src/`） |
| `System/...` | `delay`、`sys` |
| `Hardware/...` | Serial / Storage / Time / Board / Modem |
| `App/...` | Pass / Cloud / Store / Link |
| `Middlewares/` | cJSON、sha1 |
| `User/` | 见下方完整清单 |

#### `User/` 完整拷贝清单（旧 `USER/` → 新 `User/`）

| 文件 | 说明 |
| --- | --- |
| `main.c` | 入口 |
| `main.h` | |
| `board_init.c` | 板级初始化 |
| `board_init.h` | |
| `app_run.c` | `app_init` / `app_poll` |
| `app_run.h` | |
| `app_config.h` | 应用宏 |
| `stm32f4xx_it.c` | 中断服务 |
| `stm32f4xx_it.h` | |
| `stm32f4xx_conf.h` | SPL 外设头开关 |
| `stm32f4xx.h` | 器件/外设总头（本仓库自带副本） |
| `vscode_intellisense_compat.h` | Cursor/VSCode 索引用；Keil 编译可不加进工程 |

**不要拷进 `User/`：**

| 文件 | 去向 |
| --- | --- |
| `system_stm32f4xx.c`、`system_stm32f4xx.h` | → `Start/` |
| `Template.uvprojx`、`*.uvoptx`、`*.uvguix.*` | 不拷；用 New Project 新建 |
| `G4GProcess.md`、`*.mmd`、`*.puml`、`readme.md` | 可选 → `docs/`，不参与编译 |
| `RTE/` 目录（若有） | 不拷 |

拷完先别开 Keil，用资源管理器核对一遍文件夹非空。

---

## 第三步：New Project

1. 打开 Keil µVision。  
2. **Project → New µVision Project…**  
3. 浏览到 **`tikong_jx/User/`**，文件名例如 `tikong.uvprojx`，保存。  
4. 芯片选：**STM32F407VE**（与旧工程 `STM32F407VETx` 一致；列表里选 F407VE 即可）。  
5. 若弹出 **Manage Run-Time Environment**：  
   - **不要**勾 CMSIS/HAL 一大堆去「自动生成工程」。  
   - 本工程用自己拷来的 **SPL（`Library`）+ `Start` 启动文件**。  
   - 可直接 **Cancel** 关掉 RTE，或全部不勾点 OK。  
6. 若工程里自动出现了 Pack 自带的 `startup_*.s` / `system_*.c`：  
   - 在 Project 窗口里 **Remove** 掉它们，后面改用你拷到 `Start/` 里的文件。

---

## 第四步：Target 必选项（对照旧工程抄）

右键 Target → **Options for Target**。

### 4.1 Device

- Device：`STM32F407VE`（已选则不动）

### 4.2 C/C++ → Define（必填，与旧工程相同）

```text
STM32F40_41xxx,USE_STDPERIPH_DRIVER
```

两个宏都要有，中间英文逗号。

### 4.3 C/C++ → Include Paths（整段粘贴）

```text
..\Start;..\Library\inc;..\System\delay;..\System\sys;..\Hardware\Serial;..\Hardware\Storage;..\Hardware\Time;..\Hardware\Board;..\Hardware\Modem;..\Middlewares;..\App\Pass;..\App\Cloud;..\App\Store;..\App\Link;..\User
```

（工程在 `User/` 下，所以用 `..\`）

### 4.4 Output / Listing（建议）

- 勾选 Create HEX File（若你习惯烧录 hex）  
- 输出目录可设为 `..\OBJ\`（先建空 `OBJ` 文件夹）或默认

### 4.5 Debug

- 选 **J-Link**（与旧梯控工程一致；若你实际用 ST-Link 再改）  
- 设定与旧工程相同即可

### 4.6 不要用的

- 不要依赖 Pack 里的 HAL 例程替代 `Library`  
- 不要同时加两份 `system_stm32f4xx.c` 或两份 startup

---

## 第五步：建分组并「只加代码文件」

在 Project 窗口空白处右键 → **Add Group…**，建下列组，再对每组 **Add Existing Files to Group…**。

只加 **旧工程里曾经编过的** `.c` / `.s`（下表已按旧 `Template.uvprojx` 列好）。`.h` 一般**不用**加进工程（放在磁盘 + Include Path 即可）；旧工程里误加的 `wdg.h`、`uart3_at_sequence.h` 可忽略不配。

| 分组 | 添加文件（路径相对新工程根） |
| --- | --- |
| **Start** | `Start/startup_stm32f40_41xxx.s`、`Start/system_stm32f4xx.c` |
| **Library** | `Library/src/` 下与旧工程 FWLIB 组相同的那些 `.c`（见下方清单） |
| **System** | `System/delay/delay.c`、`System/sys/sys.c` |
| **Hw_Serial** | `Hardware/Serial/usart.c`、`usart2.c`、`usart3.c`、`uart4.c`、`uart5.c` |
| **Hw_Storage** | `Hardware/Storage/eeprom.c`、`24cxx.c` |
| **Hw_Time** | `Hardware/Time/RTC.c`、`ds1302.c` |
| **Hw_Board** | `malloc.c`、`bsp_hc595.c`、`ext_io.c`、`timer.c`、`floor_ctrl.c`、`led.c`、`rly.c`、`wdg.c`（均在 `Hardware/Board/`） |
| **Hw_Modem** | `Hardware/Modem/4G.c` |
| **Middlewares** | `Middlewares/cJSON.c`、`sha1.c` |
| **App_Pass** | `App/Pass/data_handler.c`、`cmd.c`、`qr_comm.c` |
| **App_Cloud** | `App/Cloud/Live_data.c`、`Live_data_r.c`、`g4g_comm.c`、`uart3_at_sequence.c` |
| **App_Store** | `App/Store/blackList.c` |
| **App_Link** | `App/Link/pc_comm.c`、`rs485_comm.c` |
| **User** | `User/main.c`、`stm32f4xx_it.c`、`board_init.c`、`app_run.c` |

### Library 组应加入的 `.c`（与旧工程一致）

均在 `Library/src/`：

`misc.c`，`stm32f4xx_adc.c`，`stm32f4xx_can.c`，`stm32f4xx_crc.c`，`stm32f4xx_cryp.c`，`stm32f4xx_cryp_aes.c`，`stm32f4xx_cryp_des.c`，`stm32f4xx_cryp_tdes.c`，`stm32f4xx_dac.c`，`stm32f4xx_dbgmcu.c`，`stm32f4xx_dcmi.c`，`stm32f4xx_dma2d.c`，`stm32f4xx_dma.c`，`stm32f4xx_exti.c`，`stm32f4xx_flash.c`，`stm32f4xx_flash_ramfunc.c`，`stm32f4xx_fsmc.c`，`stm32f4xx_gpio.c`，`stm32f4xx_hash.c`，`stm32f4xx_hash_md5.c`，`stm32f4xx_hash_sha1.c`，`stm32f4xx_i2c.c`，`stm32f4xx_iwdg.c`，`stm32f4xx_ltdc.c`，`stm32f4xx_pwr.c`，`stm32f4xx_rcc.c`，`stm32f4xx_rng.c`，`stm32f4xx_rtc.c`，`stm32f4xx_sai.c`，`stm32f4xx_sdio.c`，`stm32f4xx_spi.c`，`stm32f4xx_syscfg.c`，`stm32f4xx_tim.c`，`stm32f4xx_usart.c`，`stm32f4xx_wwdg.c`

（可多选一次加入，省事。）

**不要加入**（旧工程也未编进）：`iic.c`、`DS3231.c`（除非你后面确认需要）。

---

## 第六步：编译与修错

1. **Project → Rebuild all target files**。  
2. 缺头文件 → 查 Include Path 是否漏目录，或文件是否没拷到。  
3. `undefined symbol` → 某个 `.c` 没加进分组。  
4. `#include "../app/..."` 失败 → 改成 `#include "文件名.h"`（目录已在 Include Path）。  
5. 重复的 `SystemInit` / startup → 工程里只留 `Start` 那一份。  
6. 0 Error 后，用 J-Link 下载，看 USART1 是否有日志（与旧固件对比）。

---

## 打勾清单

- [ ] 空目录树建好  
- [ ] 代码已拷，**未**拷旧 uvprojx  
- [ ] New Project 保存在 `User/tikong.uvprojx`  
- [ ] 芯片 F407VE；Define 两宏已填  
- [ ] Include Path 已贴  
- [ ] RTE/Pack 自带 startup/system 已去掉  
- [ ] 分组文件已按表加齐  
- [ ] Rebuild 0 Error  

---

## 和完整指南的关系

| 文档 | 何时看 |
| --- | --- |
| **本文** | New Project 操作步骤（你现在这条路） |
| [从零重建江协风格工程指南.md](./从零重建江协风格工程指南.md) | 拷贝对照总表、目录含义、验收标准 |
| [目录问题与重构方案.md](./目录问题与重构方案.md) | 为什么要这样拆 |

旧工程始终保留；新工程稳定前两边可对照打开。
