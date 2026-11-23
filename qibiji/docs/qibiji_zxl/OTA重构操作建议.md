# OTA 重构操作建议（qibiji_zxl 应用侧）

> **文档定位**：OTA 的完整分析文档在 `qibiji_boot/BootLoader项目分析总纲.md`（协议逆向、风险清单 P0~P2、健壮性加固记录）。本文只覆盖 **qibiji_zxl 这一侧**：作为 A1 应用固件，它要满足 bootloader 的哪些硬接口、当前差什么、怎么补。
> **现状基线**：2026-11-21，按当前代码逐项核实（行号以本工作区为准）。

---

## 一、OTA 体系全景：三个工程的分工

| 工程 | 角色 | Flash 位置 | 与 OTA 的关系 |
|---|---|---|---|
| `qibiji_boot` | B 区 Bootloader | 0x08000000 ~ 0x0800E000（56KB） | 联网升级状态机 + A2→A1 搬运 + 跳转 A1；自身只能 SWD 烧写 |
| `qibiji_zxl`（本文对象） | **A1 应用** | 0x0800E000 起（200KB） | 被 bootloader 跳转的目标；负责开升级标志（UPDATE_flag） |
| `qibiji`（旧应用） | A1 应用（已配好 A1 地址） | 0x0800E000 起 | 可直接对照抄配置 |
| `qibiji_init` | 产线配置固件 + Qt 上位机 | 独立整片 512KB | 经 SBF-20 协议读写 EEPROM 的 OTA 组（OTA_flag / OTA_ver / UPDATE_flag），不含 OTA 逻辑 |

Flash 分区（2026-10-15 定稿，见总纲 §4.1）：

```
0x08000000  B 区  56KB   bootloader
0x0800E000  A1   200KB  运行区（当前应用）
0x08040000  A2   200KB  备份区（OTA 下载目标，复位后搬运到 A1）
0x08072000  未分配 56KB
```

数据流一句话：平台下发新固件 → bootloader 分包下载到 A2 → 复位 → bootloader 把 A2 搬到 A1 → 跳转运行。**A1 应用本身不参与下载，但要负责"下次升级的闸门"**（UPDATE_flag 置 1）。

---

## 二、应用侧必须满足的 4 条硬接口

bootloader 跳转 A1 时只做「设 MSP + 跳复位向量」（总纲 P0-4：不重定位 VTOR、不关看门狗、不复位外设），以下责任全部落在应用侧：

### ① 链接地址：IROM1 起始必须 = 0x0800E000

- 依据：bootloader 跳转目标 `LOAD_A(ST32_A1_SADDR)` = 0x0800E000（`qibiji_boot/HARDWARE/BOOT/boot.h`，按页数计算）；
- 镜像链接在别的地址，跳过去后 `.data` 初始化、向量表、常量全错位，**必死**；
- B 区链接上限已收紧到 56KB（越界链接期报错），同理应用应把 IROM1 Size 设为 0x32000（=A1 的 200KB），防止应用长大后静默压到 A2。

### ② 向量表偏移：VECT_TAB_OFFSET = 0x0E000

- 依据：`LOAD_A()` 里对 `SCB->VTOR` **只打印不修改**（boot.c:75）。应用启动后 `SystemInit()` 执行 `SCB->VTOR = FLASH_BASE | VECT_TAB_OFFSET`；
- 若偏移是 0x0，向量表仍指向 0x08000000（B 区 bootloader 的向量表）——应用一开中断就跳进 bootloader 的中断服务，行为不可预期；
- 旧应用已正确配置（`qibiji/USER/system_stm32f10x.c:128` = `0x0E000`），**照抄即可**。

### ③ 看门狗无缝接管

- bootloader 的 IWDG = 3.18s（`IWDG_Init(4,1990)`），且**只在 UPDATE_flag==1 的 OTA 路径启动**；跳转后 IWDG 不可关闭，应用必须在 3.18s 内完成初始化并开始喂狗；
- qibiji_zxl 现状：`USER/main.c:73` `WDG_Init()`（同为 3.18s）+ 74 行先喂一次 + 主循环每轮喂（108 行）——**已满足**，无需改。

### ④ 版本与升级标志链路

- bootloader 安装成功后**自动把 UPDATE_flag 置 0**（EEPROM 240，`OtaCloseUpdateGate()`）→ 之后每次上电直接跳 A1，不再联网；
- **再次升级必须有人把 UPDATE_flag 置回 1**。约定通道：平台 → A1 应用 → EEPROM 240。A1 应用需要一个能写 EEPROM OTA 组的近场/远程通道（见阶段二）；
- 版本上报来源：bootloader 的 `putVersion` 上报的是 **EEPROM 里的 OTA_ver**（flag=A1 时），不是应用代码里的版本字符串——两者的同步策略见阶段三。

---

## 三、qibiji_zxl 现状核对表（2026-11-21 逐项核实）

| # | 硬接口 | 现状 | 证据 |
|---|---|---|---|
| 1 | IROM1 起始 0x0800E000 | **✗ 未满足**：链接在 0x08000000 | `USER/SRTUV10.uvprojx:275` `<StartAddress>0x8000000</StartAddress>`（Size 已是 0x32000）；`USER/QIBIJI.map:1972` `LR_IROM1 (Base: 0x08000000 ... Max: 0x32000)` |
| 2 | VECT_TAB_OFFSET 0x0E000 | **✗ 未满足**：仍是默认 0x0 | `USER/system_stm32f10x.c:128` `#define VECT_TAB_OFFSET  0x0`；main.c 无其它 VTOR 设置 |
| 3 | 看门狗 3.18s + 早期喂狗 | ✓ 已满足 | `USER/main.c:73-74`（Init + Feed）、108 行主循环喂 |
| 4 | UPDATE_flag 写入通道 | **✗ 未迁移** | qibiji_zxl 无 `sbf20.c/sbf20_app.c`（`USER/QIBIJI.map` 与 `rebuild_a1.log` 里的 sbf20 是**历史构建残留**，当前 HARDWARE 下无此文件、未参与链接）；EEPROM OTA 组（200~243）无任何代码访问 |
| 5 | 版本字符串 | 部分 | `USER/app_info.h:4` `APP_FW_VERSION "V1.0.0"`（LCD 显示用，`lcd_pages.c:525`）；与 EEPROM OTA_ver **无联动** |

补充说明（避免误读）：`USER/PWM.map`、`USER/rebuild_a1.log`、`OBJ/PWM.lnp` 中出现的 `sbf20.o/sbf20_app.o` 来自早期某次带 SBF-20 的构建，**对应源文件现已不在工程里**，属陈旧产物，核对时以 `QIBIJI.map`（当前产物）为准。

> **与旧应用对比**：旧工程 `qibiji/USER/SRTUV10.uvprojx:275` 已是 `0x800e000`、`system_stm32f10x.c:128` 已是 `0x0E000`。即旧应用在 2026-10-15 分区变更时已按 A1 配好，qibiji_zxl 重构时这两处配置没有带过来。

---

## 四、重构操作建议（分三阶段，每阶段一个变化，未动任何代码）

### 阶段一：地址与向量表（最小改动，2 处，照抄旧工程）

**改动 1** — `qibiji_zxl/USER/SRTUV10.uvprojx` 第 275 行：

```xml
<!-- 原 -->
<StartAddress>0x8000000</StartAddress>
<!-- 改为（与 qibiji/USER/SRTUV10.uvprojx:275 一致） -->
<StartAddress>0x800e000</StartAddress>
```

Size 行（276 行 `0x32000`）不动——它正好等于 A1 的 200KB 上限。

**改动 2** — `qibiji_zxl/USER/system_stm32f10x.c` 第 128 行：

```c
/* 原 */
#define VECT_TAB_OFFSET  0x0
/* 改为（与 qibiji/USER/system_stm32f10x.c:128 一致） */
#define VECT_TAB_OFFSET  0x0E000
```

**⚠ 台架影响，做之前想清楚**：改完后固件链接在 0x0800E000，芯片上电从 0x08000000 取向量——**B 区必须有 bootloader 才能跑起来**。两种用法：

- **联调 OTA**：先 SWD 烧 `qibiji_boot/HEXBIN/boot.bin` 到 0x08000000，再烧 qibiji_zxl 到 0x0800E000（Keil 里改下载地址或用外部工具分址烧写）；
- **继续单应用台架调试**（当前大多数工作是这个）：暂时**不要做阶段一**，或做成 Keil 双目标/双配置来回切。两处改动都很小，切换成本可控，但**每次切换必须记录当前处于哪个配置**，避免"台架上明明跑得通、现场一装 bootloader 就死"这类错位。

**验证**：重编后看 `QIBIJI.map` 的 `LR_IROM1 Base` 应为 0x0800E000；SWD 同时烧 boot + app，上电串口应看到 boot 打印 `jump to a1!` 后进入应用，且应用里 `SCB->VTOR` 读数 = 0x0800E000（可临时 printf 一次验证）。

### 阶段二：UPDATE_flag 写入通道（SBF-20 参数协议迁移）

bootloader 的升级闸门（UPDATE_flag）目前**没有任何代码能打开**——旧应用里的 SBF-20 参数协议（`sbf20.c` + `sbf20_app.c`，经蓝牙 DX-BT24 / USART3，PD8/PD9 全重映射 9600）带 `ota` 组，可读写 EEPROM 的 OTA_flag / OTA_ver / UPDATE_flag（协议定义见 `qibiji_init/README.md` 的 SBF-20 章与 EEPROM 分配表，产线工具和 `tools/sbf20_cli.ps1` 已实测通过）。

之前迁移盘点把 sbf20 归为"设计上放弃"，是基于业务功能视角；**从 OTA 闭环看它是 UPDATE_flag 的现成开闸通道**，需要重新决策。两个方案：

- **方案 A（推荐）**：从旧工程迁移 `HARDWARE/USART/sbf20.c`、`sbf20_app.c`、`usart3.c` 三件套，协议与上位机已验证，改动是纯搬运 + 接到新工程的 reg/eeprom 模块；
- **方案 B**：不开近场通道，改为在现有 4G JSON 下行指令里加一条"写 UPDATE_flag"的 op。不用加 USART3，但需要服务端协议配合，且绕过了产线工具已有的验证成果。

#### 阶段二补充：方案 B 的实现路径（`setUpdateValue` 走 4G 下行，2026-11-21 增补）

方案 B 落在 `qibiji_zxl/HARDWARE/GPRS/gprs_json.c` 的 serviceCode 分发段（201-220 行），与《远程下发重构操作建议.md》阶段三对应。四个要点：

1. **分发分支**：仿照 212-218 行 `ReSet` 的"应答先行 + 动作延后"模式——写 EEPROM 耗时，先 `GPRS_ReplyServicesPush(0, "success", "{\"updateFlag\": true}", ...)`，再延迟执行写入；
2. **标志必须落 EEPROM，不能存 RAM**：目标地址是 **EEPROM 240**（UPDATE_flag，与 bootloader `OtaCloseUpdateGate()` 同一地址，见本文 §二④）。驱动用 `AT24CXX_WriteOneByte()` / `AT24CXX_ReadOneByte()`（`HARDWARE/EEPROM/eeprom_drv.h:23-24`）；
3. **写后读回确认**：写入后必须回读一次，值不符时改应答 500 并打调试输出——"回了 success 但闸没开"比不开闸更危险（平台会误判升级已排期）；
4. **写入前置检查**：仿照 `GPRS_SetValue()` 的校准中拒绝（gprs_json.c:92-96）——电机运行中/校准中不宜触发升级流程，应答 409 拒绝。

方案 B 的验证补充到本文 §五：平台下发 `setUpdateValue` → 应答 0 → SWD 或产线工具读 EEPROM 240 确认为 1 → 复位后 boot 打印 `OTA is running!` 进入升级状态机。

> 两个方案**二选一**，不叠加。若选 B，服务端需先确认下发指令的 serviceCode 命名与参数结构（列入总纲 §8 的待确认清单）。

无变更前，OTA 只能靠产线把 UPDATE_flag 预置为 1 出厂（qibiji_init 固件默认值就是 1），**每台设备只会自动升级一次**，之后闸门关闭、再也升不了——这是阶段二要解决的本质问题。

### 阶段三：版本号联动（可选，改动很小）

现状两个版本来源互不相干：`APP_FW_VERSION`（app_info.h，LCD 显示）与 EEPROM `OTA_ver`（bootloader 上报给平台的）。若不同步，平台看到的版本和设备实际固件会对不上，升级判据（strcmp 比较 EEPROM 版本与服务端 version）随之失真。

建议策略：应用启动早期（EEPROM 可用后）比较 `APP_FW_VERSION` 与 EEPROM OTA_ver，不一致时把 OTA_ver 写成 APP_FW_VERSION（一次 32B 槽写入，走 `AT24CXX` 驱动）。这样 boot 的 putVersion 上报值永远与 A1 实际固件一致，LCD 显示与上报也天然统一。

---

## 五、验证清单（全链路跑通的标准）

1. **阶段一验证**：map 的 LR_IROM1 Base = 0x0800E000；boot + app 双镜像烧写后上电 `jump to a1!` → 应用正常运行；VTOR 读数正确；断电重启正常。
2. **看门狗接管验证**：UPDATE_flag=1 进 OTA 路径 → 中途放弃/失败跳回 A1 → 应用不被 3.18s 内复位（阶段一的跳转场景下必测，因为此时 IWDG 已在跑）。
3. **SBF-20 开闸验证**（阶段二后）：`tools/sbf20_cli.ps1 -Action write -OtaUpdateflag 1` → 读回确认 → 重启后 boot 走 OTA 状态机（打印 `OTA is running!`）。
4. **OTA 全链路**：boot putVersion → 服务端判定升级 → 分包下载 A2 → 复位搬运 A2→A1（`loading progarm file A1`）→ 进新固件 → EEPROM OTA_flag 回 A1、UPDATE_flag 已被自动置 0。
5. **回滚保护验证**：升级中途断电 → 再次上电 boot 应能自愈或重试（总纲 §10.1 的护栏 + 重试限流已实现），不会变砖。

---

## 六、风险与注意

- **bootloader 侧不要再动**：`qibiji_boot` 已做完整健壮性加固（护栏、memcmp 校验、自愈、限流，见总纲 §10），且 B 区余量仅约 16KB。本阶段所有改动都限制在 qibiji_zxl / 通道层。
- **服务端协议未确认项仍在**：OTA 下行是 JSON+二进制拼帧，`offset` 语义、CRC 字节序、设备身份字段（当前硬编码 `test-001/pk001/dev001`）等 10 项待确认清单见总纲 §8——**上真机联调 OTA 前必须与服务端逐项对齐**，否则会出现"下载完成、镜像不可用"。
- **200KB 上限**：A1=A2=200KB，当前 qibiji_zxl 代码约 51KB（QIBIJI.map Size 0xC754），余量充足，但注意 A1 写满会覆盖 A2（总纲 P0-2 的历史问题，护栏已拦）。
- **现场部署纪律**（总纲 §10.2 原警告）：未验证"平台→应用开 UPDATE_flag"链路（即本文阶段二）之前，**不要在现场批量启用"安装成功后关闭升级标志"的 bootloader 版本**——升过一次后闸门永久关闭，设备无法再升级。
