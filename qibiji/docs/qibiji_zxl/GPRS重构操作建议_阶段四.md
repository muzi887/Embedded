# GPRS（4G 上云）模块重构操作建议 · 阶段四

> 原文出自《[GPRS重构操作建议_阶段二起.md](./GPRS重构操作建议_阶段二起.md)》§9，现独立成篇。
> 阶段四依赖阶段三（心跳门槛 `s_wait_timesync=1` 必须在本阶段放行）。**可按需裁剪**——暂时不需要 OTA，只做对时 + 配置即可。

## 目标

补齐对时（RTC 校准）、远程配置、版本号上报、OTA 框架。这一阶段依赖 EEPROM/Flash，**可以按需裁剪**——如果暂时不需要 OTA，只做对时 + 配置即可。

## 1. 对时（sys.timeSync）

**作用**：从平台拿 `serverTime`（13 位毫秒），写进 DS1302 硬件 RTC，让所有上报带正确时间戳。原工程直接调 `ds1302_settime()` 写芯片；本版保持同样硬件路径，但通过一个**薄抽象层**隔离 DS1302 寄存器细节——将来换 STM32 内置 RTC 或其他芯片时，只要改后端文件，对时逻辑一行不动。

### 新增文件清单

| 文件 | 职责 | 来源 |
|---|---|---|
| `HARDWARE/DS1302/ds1302.h` / `.c` | DS1302 GPIO + 字节级读写 | **直接从原工程 `qibiji/HARDWARE/DS1302/` 拷贝**，引脚 PC6(RST)/PC7(DAT)/PC8(CLK) 与原工程一致 |
| `HARDWARE/GPRS/gprs_rtc.h` | 统一时钟后端接口（抽象层） | 新建 |
| `HARDWARE/GPRS/gprs_rtc.c` | DS1302 后端实现 | 新建 |

> **注意**：zxl 工程里曾有一个 typo 文件 `gpsr_rtc.h`（gpsr → gprs），用本版 `gprs_rtc.h` 替换它。

### `gprs_rtc.h`（抽象层接口）

```c
#ifndef __GPRS_RTC_H
#define __GPRS_RTC_H

#include "gprs_config.h"

/* 从 unix 秒写 RTC（后端调 ds1302_settime 前先做 unix → BCD 转换） */
void     GPRS_RTC_Set(uint32_t unix_sec);
/* 读当前 unix 秒（后端从 ds1302_dataread 读 BCD → 转 unix） */
uint32_t GPRS_RTC_GetUnixSec(void);
/* 返回当前秒内的毫秒值（用 g_sys_ms 与上一次秒边界的差计算） */
uint16_t GPRS_RTC_GetMsOfSec(void);

/* 硬件初始化（调 ds1302_init），放在 main 初始化阶段 */
void GPRS_RTC_Init(void);

#endif
```

三个函数**全部走 DS1302 后端**，没有软件桩 fallback——因为这块板确实有 DS1302。软件 fallback 只有在将来某块板不带 DS1302 时才需要，到那时在 `gprs_rtc.c` 里用 `#ifdef HAS_DS1302` 分支隔离即可。

### `gprs_rtc.c`（DS1302 后端实现要点）

核心是两个转换：`unix_sec → ds1302 BCD`（Set 方向）和 `ds1302 BCD → unix_sec`（Get 方向）。原工程的 `ds1302_settime()` / `ds1302_dataread()` 已经处理了 BCD ↔ 十进制的低位字节，我们需要在外面包一层 unix 转换：

```
GPRS_RTC_Set(unix_sec):
    unix_sec → (year, mon, day, hour, min, sec)     // 纯算法，不依赖任何硬件
    → 调 ds1302_settime([sec, min, hour, day, mon, week, year])

GPRS_RTC_GetUnixSec():
    调 ds1302_dataread(ttt)     // ttt[0..6] = 秒/分/时/日/月/周/年（已转十进制）
    → (year, mon, day, hour, min, sec) → unix_sec    // 纯算法
    返回值
```

`GPRS_RTC_GetMsOfSec()` 用 `g_sys_ms` 与"本次 `GetUnixSec` 返回的秒值被读取时的毫秒基准"做差，范围 0–999。需要一个内部静态变量记录上一次秒边界的 `g_sys_ms`。

### main 初始化顺序调整

DS1302 初始化要放在 GPRS 之前（对时成功才放行心跳），放在 timer 之后（需要 `g_sys_ms`）：

```c
int main(void)
{
    /* ... 已有外设 init ... */
    TIM4_Init();                 // g_sys_ms 开始走
    GPRS_PORT_Init(115200);
    GPRS_RTC_Init();             // 新增：ds1302_init()
    GPRS_4G_Init();
    /* ... */
}
```

### 对时逻辑（放进 `gprs_json.c`）三条要点（照搬原工程，避开原工程 F1）

1. **发**：进 NORMAL 热身 2s 后发一轮 `request` 帧（`requestCode="sys.timeSync"`，id 用新时间戳）。模块开 `AT+CACHEN=OFF` 不缓存上行，发早了会被丢——所以必须等 MQTT 会话建好。
2. **收**：平台回 `type="request_reply"`，`body.output.serverTime` 是 13 位毫秒。解析后 `GPRS_RTC_Set(serverTime/1000)`，然后调 `NetLiveness_OnTimeSyncOk()` 放行首帧心跳。
3. **失败处理**（**这里修掉 F1**）：`body.code != 0`（业务失败）时，也要 `NetLiveness_OnTimeSyncFail()` 并重试，否则心跳被锁死 24 小时。超时重试节奏 15s/30s/60s，每次换**新的 id**（避免平台去重吃掉重发）。

### 为什么要有心跳门槛

心跳一旦通了 `s_miss` 就清零，会掩盖校时失败。校时失败会让之后所有上报带错时间戳，比"晚几分钟上线"糟得多。所以顺序硬性固定：3 条 AT → 校时 → 心跳。

### 实施位置

对时的"发"侧放在 `gprs_at.c` NORMAL 状态进入后的 2s 热身窗口（或放在 `GPRS_Process` 末尾）；"收"侧的 `request_reply` 分发要补进 `GPRS_JSON_ParseProcess` 的 switch 分支里，在 stage 2 留下的占位注释处。

## 2. 远程配置 / 版本号 / ReSet

- `getConfig` / `config`：原工程读写 `armReg.motor_warning_curr_upper`、`gate_maxheight` 并存 EEPROM。新工程无 EEPROM → 阶段四若要做，先用 RAM 变量 + 编译期初值，落在 `gprs_config.c` 里；持久化列为后续。
- `getFirmware`：回一个编译期版本号宏 `GPRS_FW_VERSION`，不依赖 OTA_Info。
- `ReSet`：阶段二已实现（延后复位），阶段四无需额外改动。

## 3. OTA（框架占位，可不做）

原工程 OTA 依赖 `24CXX`（存 OTA_Info）+ `STMFLASH`（写固件）+ A1/A2 双区 + 复位向量跳转，是独立的大需求。本版**不实现下载**，只保留 `setUpdateValue` 的回执框架（回 `{"updateFlag":...}`，标志位存 RAM）。真要做 OTA 时单独生成建议，不塞进本轮。

## 4. 阶段四验证

1. 对时：平台回 `request_reply` + `serverTime` 后，调试口打印解析出的时间，`NetLiveness_IsWaitingTimeSync()` 变 0、首帧心跳发出。
2. 对时业务失败（`code!=0`）：确认会重试而不是锁死（F1 已修）。
3. `getFirmware` → 回版本号；`getConfig` → 回当前配置项。
