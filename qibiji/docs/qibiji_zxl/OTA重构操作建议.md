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
| 4 | UPDATE_flag 写入通道 | **✓ 已实现并实测通过（2026-11-23）** | 4G config 通道 `OTAEnableFlagConfig`，gprs_json.c 宏+`GPRS_SetUpdateValue()`+分发分支（坐标见阶段二），真机验证 UPDATE_flag 置 1 成功。历史遗留：`USER/QIBIJI.map` 与 `rebuild_a1.log` 里的 sbf20 为陈旧构建产物，当前 HARDWARE 下无此文件 |
| 5 | 版本字符串 | 部分 | `USER/app_info.h:4` `APP_FW_VERSION "V1.0.0"`（LCD 显示用，`lcd_pages.c:525`）；与 EEPROM OTA_ver **无联动** |

补充说明（避免误读）：`USER/PWM.map`、`USER/rebuild_a1.log`、`OBJ/PWM.lnp` 中出现的 `sbf20.o/sbf20_app.o` 来自早期某次带 SBF-20 的构建，**对应源文件现已不在工程里**，属陈旧产物，核对时以 `QIBIJI.map`（当前产物）为准。

> **与旧应用对比**：旧工程 `qibiji/USER/SRTUV10.uvprojx:275` 已是 `0x800e000`、`system_stm32f10x.c:128` 已是 `0x0E000`。即旧应用在 2026-10-15 分区变更时已按 A1 配好，qibiji_zxl 重构时这两处配置没有带过来。

---

## 四、重构操作建议（分三阶段，每阶段一个变化；阶段二、阶段三均已落盘并实测通过）

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

#### SWD 烧写配置（F103VET6，实测可用）

目标芯片 STM32F103VET6（512K 大容量 Flash，64K RAM），调试器 ST-LINK/V2，SWD 四线（SWDIO=PA13 / SWCLK=PA14 / GND / 3V3）。boot 和 app 两个工程**各自**配好下面三处，然后各点一次 Download（IROM1 起始不同，互不覆盖对方地盘）：

1. **Options → Debug**：Use 选 `ST-LINK Debugger` → Settings → Port 选 **SW**（不是 JTAG），应能读到 IDCODE（Cortex-M3 为 `0x1BA01477`）。Connect=Normal / Reset=Autodetect 即可；
2. **Options → Debug → Settings → Flash Download**：Download Function 选 **Erase Sectors**（绝不能 Erase Full Chip，否则会把另一块分区一起擦掉）+ Program + Verify；
3. **Flash Download 的 Programming Algorithm**：选 `STM32F10x High-density Flash`（512K，0x08000000~0x0807FFFF，与 VET6 匹配）；**RAM for Algorithm 必须为 `Start: 0x20000000 / Size: 0x5000`**——VET6 有 64K RAM，这个算法需要 20KB 运行内存，给小了（如 0x1000）会报 `Flash Download failed - Cortex-M3`。

烧写成功的标志：Build Output 依次出现 `Erase: OK / Program: OK / Verify: OK`。先烧 boot（0x08000000）再烧 app（0x0800E000），顺序无强制但缺一不可。

### 阶段二：UPDATE_flag 写入通道（4G 下行，与原工程同方案）

bootloader 的升级闸门（UPDATE_flag）目前**没有任何代码能打开**——无变更前 OTA 只能靠产线把 UPDATE_flag 预置为 1 出厂（qibiji_init 固件默认值就是 1），**每台设备只会自动升级一次**，之后闸门关闭、再也升不了。

**选定方案：UPDATE_flag 走 4G 下行远程开闸（2026-11-23 定）。** 按本工程物模型设计，OTA 使能开关归属**配置（config）通道**；写入动作本身沿用原工程已验证的链路逻辑——`qibiji/HARDWARE/GPRS/gprs_JSON.c:1257` 的 `reply_cjson_update()`：收到开/关指令 → 置 UPDATE_flag → 写 EEPROM（`AT24CXX_Write(OTA_BYTE_ADDR, ...)`）→ 写后读回确认 → 应答上行。qibiji_zxl 不迁蓝牙 sbf20 通道，仿照此逻辑实现 config 通道的 `OTAEnableFlagConfig` 处理即可。

#### 实现路径与完整代码（config 通道 `OTAEnableFlagConfig`）

落在 `qibiji_zxl/HARDWARE/GPRS/gprs_json.c`，共五处改动（C90、ASCII 注释，与工程现状一致）。与《远程下发重构操作建议.md》阶段三对应。要点：

1. **写入量只有 1 字节**（UPDATE_flag @ EEPROM 240，约 10ms），可在同一拍内完成"写 → 读回确认 → 应答"，无需像 `ReSet` 那样应答先行、延迟动作——这样读回失败才能如实回 500。若以后改成整块写 OTA 组（44B）再考虑应答先行；
2. **标志必须落 EEPROM，不能存 RAM**：EEPROM 240 与 bootloader `OtaCloseUpdateGate()` 同址（见本文 §二④）；
3. **写后读回确认**：读回不符时改应答 500——"回了 success 但闸没开"比不开闸更危险（平台会误判升级已排期）；
4. **前置检查**：电机运行中/校准中拒绝（409），升级流程随即复位，不宜带着运动状态进。

**① 宏 + 命令函数**（✓ 已落盘：gprs_json.c 宏 120-123 行、`GPRS_SetUpdateValue()` 129-158 行，位于 `GPRS_StopGate()` 之后）：

```c
/* ---- OTA update gate (UPDATE_flag @ EEPROM 240) ---- */
#define OTA_UPDATE_FLAG_ADDR   240u    /* same byte as boot OtaCloseUpdateGate() */
#define OTA_UPDATE_FLAG_ON     1u
#define OTA_UPDATE_FLAG_OFF    0u

/* ---- command: setUpdateValue ---------------------------------------------
 * platform opens/closes the OTA update gate remotely.
 * Reference: old app reply_cjson_update() (qibiji/HARDWARE/GPRS/gprs_JSON.c:1257).
 * Single-byte EEPROM write (~10ms): write, verify by read-back, then reply,
 * so a failed write can honestly answer 500 instead of success.            */
void GPRS_SetUpdateValue(int enable)
{
    uint8_t flag = (enable != 0) ? OTA_UPDATE_FLAG_ON : OTA_UPDATE_FLAG_OFF;
    uint8_t state = DCMOTOR_GetState();

    /* refuse while motor is moving or calibration owns the machine:
       the update path resets the device right after */
    if ((state == MOTOR_UP) || (state == MOTOR_DOWN) || DCMOTOR_IsCalibrating())
    {
        GPRS_Printf("[OTA] setUpdateValue rejected: motor busy\r\n");
        GPRS_ReplyServices(409, "motor busy, command rejected", "{}");
        return;
    }

    AT24CXX_WriteOneByte(OTA_UPDATE_FLAG_ADDR, flag);

    /* verify by read-back: replying success with a closed gate is worse
       than refusing -- the platform would schedule an update that never runs */
    if (AT24CXX_ReadOneByte(OTA_UPDATE_FLAG_ADDR) != flag)
    {
        GPRS_Printf("[OTA] update flag write-verify failed\r\n");
        GPRS_ReplyServices(500, "eeprom verify failed", "{}");
        return;
    }

    GPRS_Printf("[OTA] UPDATE_flag set to %u\r\n", (unsigned)flag);
    GPRS_ReplyServices(0, "success",
                       (enable != 0) ? "{\"updateFlag\": true}"
                                     : "{\"updateFlag\": false}");
}
```

**② 头文件声明**（✓ 已落盘：gprs_json.h:24）：

```c
void GPRS_SetUpdateValue(int enable);
```

**③ include**（✓ 已落盘：gprs_json.c:16）：

```c
#include "eeprom_drv.h"   /* AT24CXX_WriteOneByte / ReadOneByte */
```

**OTA 开闸在物模型里归属配置（config）通道**——物模型同时设计了服务（service invoke）与配置（config push）两条下行通道：服务承载动作类指令（setValue/stopGate 等），配置承载参数与开关类下发，OTA 使能开关属于后者。平台实际格式（2026-11-23 真机日志实测确认）：

- 类型：`type:"config"`，投到订阅主题 2 `down/config/push`；
- 指令名：`body.configCode = "OTAEnableFlagConfig"`（配置通道用 configCode，服务通道才用 serviceCode）；
- 参数：`body.params.OTAEnableFlag`（bool）。

```json
{"id":"...","productKey":"pk001","deviceKey":"dev001","type":"config",
 "body":{"configCode":"OTAEnableFlagConfig","configVersion":"1.3","params":{"OTAEnableFlag":true}}}
```

该帧能通过 `GPRS_ValidateServerJson()`（只查 id/type/pk/dk 信封）。当前分发只有 `type=="service"` 入口，config 帧会被静默丢弃、不应答（真机日志：10:01:00 收到该帧后无回包，平台超时）——所以要在 service 分发段**并列**加一个 config 分支。

**④ 函数头部声明区**（✓ 已落盘：gprs_json.c:204，与 serviceCode 合并成一条声明；原 124 行文件级全局已删）：

```c
  cJSON *configCode, *serviceCode;   /* config push: thing-model configCode */
```

**⑤ 分发分支**（✓ 已落盘：gprs_json.c 345-369 行——与 `type=="service"` 的 if 并列、`cJSON_Delete(root)` 统一出口之前；声明区落盘后行号已下移）：

```c
    /* ---- config channel: thing-model config push. OTA gate flag arrives
       here as configCode OTAEnableFlagConfig (down/config/push) ---- */
    if (type && cJSON_IsString(type) && type->valuestring &&
        strcmp(type->valuestring, "config") == 0)
    {
        body = cJSON_GetObjectItem(root, "body");
        configCode = body ? cJSON_GetObjectItem(body, "configCode") : NULL;

        if (configCode && cJSON_IsString(configCode) && configCode->valuestring &&
            strcmp(configCode->valuestring, "OTAEnableFlagConfig") == 0)
        {
            /* echo configCode in the reply envelope's serviceCode field */
            strncpy(g_json_service.serviceCode, configCode->valuestring,
                    sizeof(g_json_service.serviceCode) - 1u);

            params = cJSON_GetObjectItem(body, "params");
            val = params ? cJSON_GetObjectItem(params, "OTAEnableFlag") : NULL;
            if (cJSON_IsBool(val))
                GPRS_SetUpdateValue(cJSON_IsTrue(val) ? 1 : 0);
            else
                GPRS_ReplyServices(400, "params.OTAEnableFlag(bool) required", "{}");
        }
        /* unknown configCode: ignore silently (same policy as unknown serviceCode) */
    }
```

> 下发格式（实测确认，总纲 §8 该项可勾）：`type:"config"` / `configCode:"OTAEnableFlagConfig"` / `params.OTAEnableFlag`（bool），帧内另带 `configVersion`（实测 "1.3"，设备不校验）。
> **应答格式已实测确认（2026-11-23）**：回现有 `service_reply`（topic 6 `up/service/reply`）平台接受并判定成功，无需专门的 config 确认格式。
> 实测细节：应答 `serviceCode` 被 16 字节缓冲截断为 `"OTAEnableFlagCo"`，平台未拒绝；若日后平台强校验完整 configCode，需调整回包字段来源。

验证见本文 §五：平台下发 OTAEnableFlagConfig → 应答 0 → SWD 或产线工具读 EEPROM 240 确认为 1 → 复位后 boot 打印 `OTA is running!` 进入升级状态机。

### 阶段三：版本号联动（可选，改动很小）

现状两个版本来源互不相干：`APP_FW_VERSION`（app_info.h，LCD 显示）与 EEPROM `OTA_ver`（bootloader 上报给平台的）。若不同步，平台看到的版本和设备实际固件会对不上，升级判据（strcmp 比较 EEPROM 版本与服务端 version）随之失真。

策略：应用启动早期（EEPROM 自检通过后）比较两者，不一致时把 OTA_ver 写成 APP_FW_VERSION。boot 的 putVersion 上报值（flag=A1 时）随之永远与 A1 实际固件一致。

**① 同步函数**（gprs_json.c，插在 `GPRS_SetUpdateValue()` 结束的 158 行 `}` 与 160 行 `/* ---- parse & dispatch ---- */` 之间）：

```c
/* ---- OTA version sync: APP_FW_VERSION -> EEPROM OTA_ver ---- */
#define OTA_VER_ADDR   204u   /* OTA_ADDR(200) + OTA_OFF_VER(4), u8[32] */
#define OTA_VER_LEN    32u

void GPRS_SyncOtaVersion(void)
{
    uint8_t ee_ver[OTA_VER_LEN];
    uint8_t fw_len;
    uint8_t i;
    uint8_t match;

    fw_len = (uint8_t)strlen(APP_FW_VERSION);
    if (fw_len >= OTA_VER_LEN)
        return;                        /* 槽放不下则不改（当前 APP_FW_VERSION 为短串） */

    AT24CXX_Read(OTA_VER_ADDR, ee_ver, OTA_VER_LEN);
    match = (strncmp((const char *)ee_ver, APP_FW_VERSION, fw_len) == 0) &&
            (ee_ver[fw_len] == 0u);
    if (match)
        return;                        /* 已一致：不写，省 EEPROM 擦写 */

    for (i = 0u; i < OTA_VER_LEN; i++)
        ee_ver[i] = (i < fw_len) ? (uint8_t)APP_FW_VERSION[i] : 0u;   /* 版本串 + 0 填满 32B */

    AT24CXX_Write(OTA_VER_ADDR, ee_ver, OTA_VER_LEN);

    /* verify by read-back */
    AT24CXX_Read(OTA_VER_ADDR, ee_ver, OTA_VER_LEN);
    match = (strncmp((const char *)ee_ver, APP_FW_VERSION, fw_len) == 0) &&
            (ee_ver[fw_len] == 0u);
    if (match)
        GPRS_Printf("[OTA] OTA_ver synced to %s\r\n", APP_FW_VERSION);
    else
        GPRS_Printf("[OTA] OTA_ver sync write-verify failed\r\n");
}
```

**② 头文件声明**（gprs_json.h，24 行 `GPRS_SetUpdateValue` 声明的下一行）：

```c
void GPRS_SyncOtaVersion(void);
```

**③ 调用点**（main.c，92 行 `GprsCfg_Init();` 之后加一行）：

```c
GPRS_SyncOtaVersion();   /* APP_FW_VERSION 同步到 EEPROM OTA_ver（boot putVersion 上报用） */
```

---

## 五、验证清单（全链路跑通的标准）

1. **阶段一验证**：map 的 LR_IROM1 Base = 0x0800E000；boot + app 双镜像烧写后上电 `jump to a1!` → 应用正常运行；VTOR 读数正确；断电重启正常。
2. **看门狗接管验证**：UPDATE_flag=1 进 OTA 路径 → 中途放弃/失败跳回 A1 → 应用不被 3.18s 内复位（阶段一的跳转场景下必测，因为此时 IWDG 已在跑）。
3. **4G 开闸验证**（阶段二后）：平台经 **config 通道**下发 `OTAEnableFlagConfig`（`params.OTAEnableFlag=true`）→ 应答 0 且读回 EEPROM 240 确认为 1 → 重启后 boot 走 OTA 状态机（打印 `OTA is running!`）。
   - **状态（2026-11-23）**：✅ **全链路已实测通过**——阶段三版本联动首启 `[OTA] OTA_ver synced to V1.0.0`，重启后 boot 打印 `OTA_ver = V1.0.0,a1a1a1a1,255`（版本号持久化 + A1 魔数 + UPDATE_flag 空白值 0xFF）；阶段二开闸 14:06 真机实测：收到 config 帧后 `[OTA] UPDATE_flag set to 1`，`service_reply`（output `updateFlag:true`）上行，平台判定成功。此前"回包格式是否被平台接受"的待确认项关闭。
   - **开闸后重启端到端确认（14:0x）**：重启后 boot 读到 UPDATE_flag=1，LCD 进入 OTA 状态机文案（"在线升级 → 连接成功 → 申请最新固件中"，即 ota_step1/step2），putVersion 上报 EEPROM 版本等待服务器比对——开闸链路端到端生效。两个已知行为：① boot 在线判断只靠 LINKA 边沿中断（P1-7），上电时模块已在线无跳变会等约 40s 超时再跳 A1；② 无升级/放弃升级**不关闸**（防弱网复位循环的设计），UPDATE_flag 保持 1，之后每次上电都重走 OTA 流程——需要关闭时平台下发 `OTAEnableFlagConfig=false`（写 0），或等一次真实升级成功后 boot 自动置 0。
4. **OTA 全链路**：boot putVersion → 服务端判定升级 → 分包下载 A2 → 复位搬运 A2→A1（`loading progarm file A1`）→ 进新固件 → EEPROM OTA_flag 回 A1、UPDATE_flag 已被自动置 0。
5. **回滚保护验证**：升级中途断电 → 再次上电 boot 应能自愈或重试（总纲 §10.1 的护栏 + 重试限流已实现），不会变砖。

---

## 六、风险与注意

- **bootloader 侧不要再动**：`qibiji_boot` 已做完整健壮性加固（护栏、memcmp 校验、自愈、限流，见总纲 §10），且 B 区余量仅约 16KB。本阶段所有改动都限制在 qibiji_zxl / 通道层。
- **服务端协议未确认项仍在**：OTA 下行是 JSON+二进制拼帧，`offset` 语义、CRC 字节序、设备身份字段（当前硬编码 `test-001/pk001/dev001`）等 10 项待确认清单见总纲 §8——**上真机联调 OTA 前必须与服务端逐项对齐**，否则会出现"下载完成、镜像不可用"。
- **200KB 上限**：A1=A2=200KB，当前 qibiji_zxl 代码约 51KB（QIBIJI.map Size 0xC754），余量充足，但注意 A1 写满会覆盖 A2（总纲 P0-2 的历史问题，护栏已拦）。
- **现场部署纪律**（总纲 §10.2 原警告）：未验证"平台→应用开 UPDATE_flag"链路（即本文阶段二）之前，**不要在现场批量启用"安装成功后关闭升级标志"的 bootloader 版本**——升过一次后闸门永久关闭，设备无法再升级。
