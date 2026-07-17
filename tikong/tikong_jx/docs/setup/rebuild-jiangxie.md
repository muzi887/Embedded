# 从零重建江协风格工程 — 操作指南

更新时间：2026-07-17  
状态：**仅说明，不自动改旧工程**  
适用：旁路新建目录树并拷贝源码  

> **若你选 Keil → New Project、只拷代码**：请主读  
> [Keil-New-Project只拷代码指南.md](./keil-new-project.md)（本文作拷贝总表与目录说明）。

> 目标树与业务拆分仍以 [目录问题与重构方案.md](./dir-refactor-plan.md) 为准。  
> 旧工程 `tikong/tikong` **整份保留作对照**，不要删。

---

## 〇、为什么用「重建」而不是「改目录名」


| 方式          | 痛点                                                        |
| ----------- | --------------------------------------------------------- |
| 原地改名（R1～R3） | Keil `Template.uvprojx` 里上百条相对路径、Include Path 全要改，漏一条就编不过 |
| **新建工程再拷贝** | 空目录一次建对；Keil 分组按新树 **重新添加文件**；旧工程始终可回退对照                  |


重建 = **新文件夹 + 拷贝 `.c/.h` + 新配 Keil**，不是重写业务逻辑。源文件名可先保持不变（与已批准方案一致）。

---

## 一、你要得到的结果

在旧工程旁（推荐）新建例如：

```text
Embedded/tikong/
├── tikong/                 ← 旧工程（不要动、不要删）
└── tikong_jx/              ← 新建（江协骨架名可自定）
    ├── User/
    ├── System/
    ├── Hardware/
    │   ├── Serial/
    │   ├── Storage/
    │   ├── Time/
    │   ├── Board/
    │   └── Modem/
    ├── App/
    │   ├── Pass/
    │   ├── Cloud/
    │   ├── Store/
    │   └── Link/
    ├── Middlewares/
    ├── Library/            ← 从旧 FWLIB 整份拷贝
    ├── Start/              ← 从旧 CORE + system_stm32 拷贝
    ├── docs/               ← 可选：拷说明文档
    └── User/Template.uvprojx   ← 新建或从旧工程另存后改
```

名称 `tikong_jx` 可改成你喜欢的；下文统称 **新工程根**。

---

## 二、前置准备

1. 旧工程能打开：`tikong/tikong/USER/Template.uvprojx`（能编最好，编不过也至少当源码仓库用）。
2. 安装 Keil MDK，已有 STM32F4 支持包 / 本工程原用的器件选项：**STM32F407VE**（或与旧工程 Device 一致）。
3. 磁盘空间足够；用资源管理器或命令行拷贝均可。
4. 建议：拷贝前把本指南与 [目录问题与重构方案.md](./dir-refactor-plan.md) 打开放在旁边对照。

---

## 三、总流程（五步）

```text
① 建空目录树
② 按对照表从旧工程拷贝文件（不改文件名）
③ 在 Keil 建工程 / 另存工程，设芯片与宏
④ 配 Include Path + 按分组添加 .c
⑤ 编译 → 修 include → 再编译，直到通过
```

下面按步展开。

---

## 四、步骤 ①：建空目录树

在新工程根下手动创建（或脚本创建）这些文件夹：

```text
User
System/delay
System/sys
Hardware/Serial
Hardware/Storage
Hardware/Time
Hardware/Board
Hardware/Modem
App/Pass
App/Cloud
App/Store
App/Link
Middlewares
Library
Start
docs
```

**先不要**往里塞文件，确认树长对即可。

---

## 五、步骤 ②：按表拷贝（只拷贝，不改名）

从 **旧工程** `tikong/tikong/` 拷到 **新工程**对应目录。  
`.c` / `.h` **文件名保持原样**。

### 5.1 Start / Library（库与启动，整包拷）


| 旧路径                                                    | 新路径                             |
| ------------------------------------------------------ | ------------------------------- |
| `CORE/`*（含 `startup_stm32f40_41xxx.s`、`core_cm4*.h` 等） | `Start/`                        |
| `USER/system_stm32f4xx.c`、`USER/system_stm32f4xx.h`    | `Start/`                        |
| `FWLIB/*`（整个 `inc` + `src`）                            | `Library/`（保持 `inc/`、`src/` 结构） |


### 5.2 System（延时与系统）


| 旧路径                              | 新路径             |
| -------------------------------- | --------------- |
| `SYSTEM/delay/delay.c`、`delay.h` | `System/delay/` |
| `SYSTEM/sys/sys.c`、`sys.h`       | `System/sys/`   |


### 5.3 Hardware


| 旧路径                                                                | 新路径                 |
| ------------------------------------------------------------------ | ------------------- |
| `SYSTEM/usart/usart*.c/h`、`uart4.*`、`uart5.*`                      | `Hardware/Serial/`  |
| `drivers/iic.*`、`eeprom.*`、`24cxx.*`                               | `Hardware/Storage/` |
| `board/RTC.*`、`drivers/ds1302.*`、`drivers/DS3231.*`                | `Hardware/Time/`    |
| `board/bsp_hc595.*`、`ext_io.*`、`timer.*`、`malloc.*`、`floor_ctrl.*` | `Hardware/Board/`   |
| `drivers/led.*`、`rly.*`、`wdg.*`                                    | `Hardware/Board/`   |
| `drivers/4G.c`、`4G.h`                                              | `Hardware/Modem/`   |


说明：旧工程 Keil 里未必把 `iic.c`、`DS3231.c` 都加进工程；**磁盘上可以都拷**，Keil 里只加旧工程已经编译过的 `.c`（见第六节分组表）。

### 5.4 App（业务拆开）


| 旧路径                                   | 新路径          |
| ------------------------------------- | ------------ |
| `app/comm/qr_comm.c`、`qr_comm.h`      | `App/Pass/`  |
| `app/utils/cmd.c`、`cmd.h`             | `App/Pass/`  |
| `app/data_handler.c`、`data_handler.h` | `App/Pass/`  |
| `app/comm/g4g_comm.c`                 | `App/Cloud/` |
| `app/Live_data.c`、`Live_data.h`       | `App/Cloud/` |
| `app/Live_data_r.c`、`Live_data_r.h`   | `App/Cloud/` |
| `app/comm/uart3_at_sequence.c`、`.h`   | `App/Cloud/` |
| `app/blackList.c`、`blackList.h`       | `App/Store/` |
| `app/comm/pc_comm.c`                  | `App/Link/`  |
| `app/comm/rs485_comm.c`               | `App/Link/`  |
| `app/comm/app_comm.h`                 | `App/Link/`  |


`AT_reference.txt`、`uart3_at_sequence_porting.md` → 拷到新工程 `docs/`（不参与编译）。

### 5.5 Middlewares


| 旧路径                             | 新路径            |
| ------------------------------- | -------------- |
| `third_party/cJSON.c`、`cJSON.h` | `Middlewares/` |
| `third_party/sha1.c`、`sha1.h`   | `Middlewares/` |


### 5.6 User（入口与工程文件）


| 旧路径                                     | 新路径                                        |
| --------------------------------------- | ------------------------------------------ |
| `USER/main.c`、`main.h`                  | `User/`                                    |
| `USER/board_init.c`、`board_init.h`      | `User/`                                    |
| `USER/app_run.c`、`app_run.h`            | `User/`                                    |
| `USER/app_config.h`                     | `User/`                                    |
| `USER/stm32f4xx_it.c`、`stm32f4xx_it.h`  | `User/`                                    |
| `USER/stm32f4xx_conf.h`                 | `User/`                                    |
| `USER/stm32f4xx.h`（若旧工程有）               | `User/`（或按你 Keil 器件包习惯放 `Start/`，二选一，见 §7） |
| `USER/vscode_intellisense_compat.h`（可选） | `User/`                                    |


**先不要**把旧的 `Template.uvprojx` 直接当最终工程用（路径全是旧的）。做法见下一步。

### 5.7 文档（可选）

把旧 `docs/` 整份拷到新工程 `docs/`，并保留旧工程不动。根目录杂 md 也可拷进 `docs/legacy/`，**不要删旧工程里的原件**。

---

## 六、步骤 ③④：Keil 工程怎么建

### 6.2 做法 B：真正「New Project」（**当前推荐**）

逐步操作见专篇（含 Define、Include、分组文件清单）：

→ **[Keil-New-Project只拷代码指南.md](./keil-new-project.md)**

摘要：工程存 `User/tikong.uvprojx`；不拷旧 uvprojx；关掉 RTE 自动组件；用自己的 `Start` + `Library`；Define 填 `STM32F40_41xxx,USE_STDPERIPH_DRIVER`。

### 6.1 做法 A：从旧工程「另存」再清空再加（备选，较快）

1. 复制旧 `USER/Template.uvprojx`（及同目录 `.uvoptx` 若有）到新工程 `User/`。
2. 用 Keil 打开 **新工程**里的该文件。
3. **Options for Target → C/C++ → Include Paths** 全部改成新路径（见 §6.3）。
4. 在 Project 窗口里：**删掉旧分组里失效的文件**（或整组 Delete Group 后重建）。
5. 按 §6.4 **新建分组**，用 *Add Existing Files to Group* 从新目录把 `.c` 加回来。
6. **Options → Device** 确认仍是 STM32F407VE（与旧工程一致）。
7. **Options → C/C++ → Define** 保持与旧工程一致（`STM32F40_41xxx,USE_STDPERIPH_DRIVER`）。
8. **Options → Linker**、调试器（J-Link）与旧工程对齐。
9. Rebuild。

### 6.3 Include Path（一次性配齐）

在 *C/C++ → Include Paths* 中加入（相对 `User/` 工程文件）：

```text
..\Start
..\Library\inc
..\System\delay
..\System\sys
..\Hardware\Serial
..\Hardware\Storage
..\Hardware\Time
..\Hardware\Board
..\Hardware\Modem
..\Middlewares
..\App\Pass
..\App\Cloud
..\App\Store
..\App\Link
..\User
```

旧工程参考值曾是：

```text
..\CORE;..\SYSTEM\delay;..\SYSTEM\sys;..\SYSTEM\usart;..\USER;..\FWLIB\inc;
..\third_party;..\drivers;..\board;..\app;..\app\utils;..\app\comm
```

新路径就是上表一一替换。

> 把头文件目录都放进 Include Path 后，源码里多数 `#include "xxx.h"` **可以不改**。  
> 若某处写成 `#include "../app/comm/app_comm.h"` 这种相对路径，编译报错时再改成 `#include "app_comm.h"`（头已在 Include Path 里）。

### 6.4 建议 Keil 分组与应添加的 `.c`


| 分组名           | 添加哪些 `.c`（来自新目录）                                                                     |
| ------------- | ------------------------------------------------------------------------------------ |
| `Start`       | `startup_stm32f40_41xxx.s`、`system_stm32f4xx.c`                                      |
| `Library`     | 与旧 `FWLIB` 分组相同的那些 `stm32f4xx_*.c`、`misc.c`（对照旧 uvprojx，不必一次加满未用外设也可，但为省事可整组照旧加）     |
| `System`      | `delay.c`、`sys.c`                                                                    |
| `Hw_Serial`   | `usart.c`、`usart2.c`、`usart3.c`、`uart4.c`、`uart5.c`                                  |
| `Hw_Storage`  | `eeprom.c`、`24cxx.c`（若旧工程编过 `iic.c` 再加）                                              |
| `Hw_Time`     | `RTC.c`、`ds1302.c`（`DS3231.c` 仅当旧工程用过再加）                                             |
| `Hw_Board`    | `malloc.c`、`bsp_hc595.c`、`ext_io.c`、`timer.c`、`floor_ctrl.c`、`led.c`、`rly.c`、`wdg.c` |
| `Hw_Modem`    | `4G.c`                                                                               |
| `Middlewares` | `cJSON.c`、`sha1.c`                                                                   |
| `App_Pass`    | `qr_comm.c`、`cmd.c`、`data_handler.c`                                                 |
| `App_Cloud`   | `g4g_comm.c`、`Live_data.c`、`Live_data_r.c`、`uart3_at_sequence.c`                     |
| `App_Store`   | `blackList.c`                                                                        |
| `App_Link`    | `pc_comm.c`、`rs485_comm.c`                                                           |
| `User`        | `main.c`、`stm32f4xx_it.c`、`board_init.c`、`app_run.c`                                 |


对照旧工程时：打开旧 `Template.uvprojx`，看每个 Group 里有哪些 `FileName`，新工程 **只加旧工程里有的**，避免多加未维护文件导致链接错误。

旧工程分组名参考：`USER`、`CORE`、`FWLIB`、`SYSTEM`、`THIRDPARTY`、`DRIVERS`、`BOARD`、以及 APP 相关组——按上表换成新名即可。

---

## 七、步骤 ⑤：编译与修错顺序

按这个顺序修，少绕路：

1. **Rebuild**，先看「找不到头文件」
  - 补 Include Path，或把漏拷的 `.h` 拷进对应目录。
2. **再编**，看「找不到函数 / undefined symbol」
  - 某个 `.c` 没加进分组；对照旧 uvprojx 补上。
3. **再编**，看 `#include "../xxx"` 相对路径失败
  - 改成只写文件名，依赖 Include Path。
4. **再编**，看重复定义 / 多份 `system_stm32` 或启动文件
  - 只保留 `Start` 里一份启动 + 一份 `system_stm32f4xx.c`。
5. 通过后：下载到板子，看 USART1 是否还有 `printf`（与旧工程最小对比）。

### 常见坑


| 现象                | 处理                                     |
| ----------------- | -------------------------------------- |
| `stm32f4xx.h` 找不到 | 确认在 `User/` 或器件包路径；Include 含 `..\User` |
| 启动文件冲突            | 只用本仓库 `Start/startup_stm32f40_41xxx.s` |
| 宏不对导致外设库报错        | Define 与旧工程逐字一致                        |
| `iic.c` 未加入但被调用   | 查旧工程是否编进；需要则加入 `Hw_Storage`            |
| 业务头互相找不到          | 确认 `App/Pass                           |


---

## 八、验收清单（重建完成标准）

- 新工程根目录外层能看到：`User` `System` `Hardware` `App` `Middlewares` `Library` `Start`
- `App/Pass` 含通行；`App/Cloud` 含云端；`App/Store` 含黑名单；`App/Link` 含 PC/485
- `Hardware/Serial` 只有驱动，没有 `*Process` 业务文件
- Keil Rebuild **0 Error**（Warning 可后续再清）
- 旧工程 `tikong/tikong` **仍在**，可随时打开对照
- （可选）板级：USART1 有日志；或与旧固件行为抽测一致

---

## 九、拷贝用检查表（打勾）

把本表打印或复制到笔记里，拷完勾选：

- `Start`：CORE + system_stm32
- `Library`：整份 FWLIB
- `System/delay`、`System/sys`
- `Hardware/Serial`：五路 usart/uart
- `Hardware/Storage`：iic/eeprom/24cxx
- `Hardware/Time`：RTC/ds1302/(DS3231)
- `Hardware/Board`：板级 + led/rly/wdg
- `Hardware/Modem`：4G
- `App/Pass`：qr_comm、cmd、data_handler
- `App/Cloud`：g4g、Live_data*、uart3_at_sequence
- `App/Store`：blackList
- `App/Link`：pc_comm、rs485_comm、app_comm.h
- `Middlewares`：cJSON、sha1
- `User`：main、board_init、app_run、it、conf
- Keil：Include Path + 各分组 `.c` + Define/Device
- Rebuild 通过

---

## 十、和「原地改名方案」的关系


| 文档                                                                       | 用途                     |
| ------------------------------------------------------------------------ | ---------------------- |
| [目录问题与重构方案.md](./dir-refactor-plan.md)                                           | 问题 + 目标树（两种实施都适用）      |
| [superpowers/plans/…目录重构.md](./superpowers/plans/2026-07-17-江协风格目录重构.md) | **原地改名**分步清单（你觉得麻烦可不用） |
| **本文**                                                                   | **推荐：旁路新建工程再拷贝**       |


不要求删旧文件、不要求立刻废弃旧工程；新工程稳定后再决定是否只维护新树。

---

## 十一、你现在只要做的事

1. 建旁路文件夹（如 `tikong_jx`）和空目录树（§四）。
2. 按 §五 从旧工程拷贝。
3. 按 §六 配 Keil。
4. 按 §七 编译修错，直到 §八 勾完。

若某一步 Keil 报错贴不出完整日志，把 **第一条 Error** 和当前 Include Path 发出来即可继续排查（仍不必改旧工程）。