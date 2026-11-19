# GPRS（4G 上云）模块重构操作建议 · 阶段四

> 原文出自《[GPRS重构操作建议_阶段二起.md](./GPRS重构操作建议_阶段二起.md)》§9，现独立成篇。
> 阶段四依赖阶段三（心跳门槛 `s_wait_timesync=1` 必须在本阶段放行）。**可按需裁剪**——暂时不需要 OTA，只做对时 + 配置即可。

## 目标

补齐对时（RTC 校准）、远程配置、版本号上报、OTA 框架。这一阶段依赖 EEPROM/Flash，**可以按需裁剪**——如果暂时不需要 OTA，只做对时 + 配置即可。

## 1. 对时（sys.timeSync）

**作用**：从平台拿 `serverTime`（13 位毫秒），写进 DS1302 硬件 RTC，让所有上报带正确时间戳。

> **完整实现已独立成篇**：[DS1302重构操作建议.md](./DS1302重构操作建议.md)。
> 那里包含驱动拷贝清单与三处已知缺陷修正、`gprs_rtc.h/.c` 全部源码（unix ↔ BCD ↔ 时区 ↔ 周计算）、对时状态机完整代码、验证清单。本节只保留要点。

### 四条要点

1. **发**：进 `ST_NORMAL` 后**先热身 2s**，再发一轮 `request`（`requestCode="sys.timeSync"`，id 用 13 位时间戳）。模块 `AT+CACHEN=OFF` 不缓存上行，发早了会被丢。热身由 `GPRS_ClockSync_OnNormal()` 起计时，`Process()` 到点才 `Start()`。
2. **收**：回执 `type="request_reply"`，`body.output.serverTime` 是 13 位毫秒，**必须用 `cJSON_GetNumberValue()` 读成 double**（`valueint` 会溢出）。写入 RTC 后调 `NetLiveness_OnTimeSyncOk()` 放行首帧心跳。
3. **失败**（修掉原工程 F1）：`body.code != 0` 时也要 `NetLiveness_OnTimeSyncFail()` 并重试，否则心跳被锁死 24 小时。重试节奏 15/30/60s，每次换新 id。
4. **通道未确认时不烧重试次数**：任何一帧下行（心跳 ack 也算）都会调 `GPRS_ClockSync_OnDownlink()` 把 `s_clock_chan_ok` 置 1，之后才走 15/30/60 正式重试；在此之前按 60s 一帧单发探测，最多 5 次后交给 `NetLiveness` 的 `timeSync stuck` 看门狗。这样即使整轮都撞在模块重连窗口里，也不会 105 秒就 `give up`。

### 为什么要有心跳门槛

心跳一旦通了 `s_miss` 就清零，会掩盖校时失败。校时失败会让之后所有上报带错时间戳，比"晚几分钟上线"糟得多。顺序硬性固定：3 条 AT → 校时 → 心跳。

### 依赖

- `GPRS_RTC_*` 四个接口来自本阶段新增的抽象层（见独立文档 §6/§7）；
- `GPRS_RTC_GetUnixSec()` 在未校时时返回 0，调用方必须判断，不能把 1970 年的时间戳发给平台。

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
