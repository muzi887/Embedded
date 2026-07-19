# 存储与持久化逻辑（阶段 E）

更新时间：2026-07-18

> **对齐**：[onboarding-f103.md](../overview/onboarding-f103.md) 阶段 E  
> **地址/API 细节**：[eeprom.md](./eeprom.md)（芯片、布局、驱动 API）  
> **AT 成功后再写 EEPROM**：[at-config-flow.md](../cloud/4g/at-config-flow.md)  
> **通行验签 / 口令漂移**：[commcontrl-permission-chain.md](../pass/commcontrl-permission-chain.md)、[password-4digit-auth.md](../pass/password-4digit-auth.md)

目标：弄清 **什么掉电还在、上电怎么装进 RAM、业务何时回写**。  
实现以固件为准；芯片布局权威源：`Hardware/Storage/eeprom.h`。

---

## 一、总览：掉电保留 vs 仅 RAM


| 类别                    | 掉电后                     | 上电装载                             | 运行时主要读哪里                              |
| --------------------- | ----------------------- | -------------------------------- | ------------------------------------- |
| 黑名单（≤200 条）           | EEPROM `0..2199`        | `loadDataFromEEPROM`             | `dataArray[]`（RAM）                    |
| 设备账号/类型/公钥/模式、漂移、MQTT | EEPROM `3754..4095`     | `ReadKey`                        | `g_device_`* / `g_mqtt_*`             |
| 限层位图 + 截止时间           | EEPROM `3758..3776`     | `ReadKey` → `FloorCtrl_SetLimit` | `FloorCtrl_*` + `g_floors_limit_time` |
| 当前时钟                  | **片外 DS1302**（非 EEPROM） | `RtcChip_GetTime`                | `app_run` 的 `currentTime` 缓存          |
| 读头/云端临时 JSON          | 否                       | —                                | 栈/缓冲，用完即弃                             |


```text
board_init()
  ds1302_init()           // 时钟芯片 GPIO（PE9/10/11）
  AT24C32_I2C_Init()      // EEPROM：硬件 I2C1，PB6/PB7，从机 0x50
  ReadKey()               // 配置区 → 全局变量 + FloorCtrl_SetLimit
      ↓
app_init()
  loadDataFromEEPROM()    // 黑名单区 → dataArray[]
  RtcChip_GetTime()       // DS1302 → currentTime（过旧则写默认时间）
  [限层未过期] Floor_AuthCheck_limit()
      ↓
main 循环
  app_poll() / 权限检查 → 读 RAM
  业务写回 → WriteKey / WriteRawBytes / add|delete 黑名单
  TIM3 约每分钟：限层截止则清 EEPROM + 关限层 IO
```

**顺序要点**：先 `ReadKey`（配置），后 `loadDataFromEEPROM`（黑名单）；两区地址互不重叠。

---

## 二、芯片与总线


| 器件                      | 接口          | 引脚（本工程）                       | 驱动                                      |
| ----------------------- | ----------- | ----------------------------- | --------------------------------------- |
| **AT24C32**（4KB EEPROM） | 硬件 **I2C1** | PB6/PB7，地址 **0x50**           | `Hardware/Storage/24cxx.`* ← `eeprom.c` |
| **DS1302**（RTC）         | 三线 bit-bang | PE9 RST / PE10 DAT / PE11 CLK | `Hardware/Time/ds1302.`* ← `RTC.c`      |


- `Hardware/Storage/iic.c`（PE2/PE3 软件 I2C）**当前 init 路径未调用**，不要按「EEPROM 与 RTC 共总线」理解现网。
- 源码里偶见 `DS3231` / `MakeTimestamp14FromDS3231` 等旧名，实际芯片是 **DS1302**。

---

## 三、AT24C32 地址布局（4096 B）

```text
0    ……  2199     黑名单：200 槽 × 11 字节
2200 ……  3753     保留（业务未用）
3754 ……  3757     时间漂移 PUBLIC_DRIFT_TIME（4，文本）
3758 ……  3762     限层位图 PUBLIC_FLOORS_LIMIT（5，二进制）
3763 ……  3776     限层截止 PUBLIC_FLOORS_LIMIT_TIME（14，YYYYMMDDhhmmss）
3777 ……  3808     设备 ID / 密码
3809             设备类型（1）
3810 ……  3825     设备编码
3826 ……  3841     公钥
3842 ……  3871     设备模式（继电器时间等）
3872 ……  4095     MQTT：addr / productKey / deviceKey / deviceSecret
```

**读规则简述**

- 文本字段：首字节 `0xFF` 常视为空（见 `EEPROM_ReadTextField` 一类逻辑）。
- **限层位图例外**：首字节 `0xFF` **不一定**是空（可能表示全开），用 `EEPROM_ReadBuffer` / `WriteRawBytes`。

黑名单单槽 11 字节：`[0..9]` ID（最多 10 字符），`[10]` 占用标记：`0x00` 有效，`0x01` 已删/空。

---

## 四、黑名单子系统


| 动作   | 函数                        | EEPROM        | RAM             |
| ---- | ------------------------- | ------------- | --------------- |
| 上电装载 | `loadDataFromEEPROM`      | 读 `0..2199`   | 填 `dataArray[]` |
| 新增   | `addDataToBlacklist`      | 写满 11 字节      | 占槽              |
| 删除   | `deleteDataFromBlacklist` | 只写标记字节 `0x01` | `isEmpty=1`     |
| 全清标记 | `clearPublicKeySpace`     | 各槽标记 → `0x01` | 全空（注意函数名易误解）    |


**通行拦截**：`Cmd_Permission_CheckBlacklist` 扫 **RAM** `dataArray`，不实时扫 EEPROM。

**入口（当前有效）**

- 云端：`parseSerialData` **case 105** 增、**106** 删（`Live_data_r.c` → `parse_addblack_json` / `parse_delblack_json`）。
- 读头 `CommContrl` 的黑名单命令路径在现码中 **未真正接通**（有 `BlackUserListAdd/Del` 等实现，但业务入口不可达）——联调增删以云端为准。

---

## 五、设备配置与 MQTT（`ReadKey` / `Cmd_Setting`）

`ReadKey()`（`board_init`）把配置区装到全局，并副作用：


| EEPROM 字段                      | 全局 / 副作用                          |
| ------------------------------ | --------------------------------- |
| 设备 id / password / type / code | `g_device_`*                      |
| 公钥                             | `g_device_public_Key`             |
| 模式                             | `g_device_mode` → 解析继电器时间         |
| 漂移                             | `g_device_drift_time`（通行/口令时间窗）   |
| MQTT×4                         | `g_mqtt_*`（给 `uart3_at_sequence`） |
| 限层位图                           | `FloorCtrl_SetLimit`              |
| 限层截止                           | `g_floors_limit_time`             |


**写回（配置区）**


| 触发                                | 行为                                                                          |
| --------------------------------- | --------------------------------------------------------------------------- |
| 读头 `Cmd_Setting` **13 列**（含 MQTT） | 先置 pending + 跑 AT；**仅当** `Cmd_Setting_OnAtSequenceDone` 成功后 `WriteKey`*，再复位 |
| 读头 `Cmd_Setting` **9 列**（无 MQTT）  | `Cmd_Setting_WriteKeysToEeprom_sim` **立即**写                                 |
| AT 中止                             | `Cmd_Setting_OnAtSequenceAbort`：清 pending，**不写**                            |
| `SetDevicePublicKey`              | 立刻写公钥区                                                                      |
| 云端改公钥（case 107）                   | 现码多只改 **RAM**，**未必落 EEPROM**（重启可能丢）                                         |


凭证校验：`VerifyDeviceCredentials` 对比 `g_device_id` / `g_device_password`。

详情见 [at-config-flow.md](../cloud/4g/at-config-flow.md)。

---

## 六、RTC 与时间相关权限

```text
DS1302 ←→ RtcChip_GetTime / SetTime
              ↓
         currentTime（app_run 缓存）
              ↓
  Refresh_CurrentTime → 拼 14 位 now → 与 begin/end、漂移比较
```


| 用途    | 要点                                                                            |
| ----- | ----------------------------------------------------------------------------- |
| 通行有效期 | `Cmd_Permission_CheckTimeWindow`：`g_device_drift_time` 放宽 begin/end 后与 now 比较 |
| 四位口令  | 另有三档漂移槽；见 [password-4digit-auth.md](../pass/password-4digit-auth.md)          |
| 校时来源  | `Cmd_Setting` / `Cmd_Set_Time` / 云端 `parse_uptime_json`（case 1）等              |
| 上电默认  | `year < 26` 时强制写成 **2026-04-20 08:57**（`app_init`）                            |


时间不准 → 有效期/口令大量失败；联调优先校 RTC，再查业务。

---

## 七、限层持久化


| 项       | 位置                                                                                           |
| ------- | -------------------------------------------------------------------------------------------- |
| 5 字节位图  | EEPROM `3758` → `FloorCtrl_SetLimit` / `GetLimit`                                            |
| 14 字符截止 | EEPROM `3763` → `g_floors_limit_time`                                                        |
| 写入口     | 读头 `Cmd_Set_OpenLimit`（cmd `'7'`）→ `WriteRawBytes` + `Floor_AuthCheck_limit`                 |
| 上电应用    | `app_init`：截止 ≥ now → 开限层 IO                                                                 |
| 到期关闭    | `main` 中 `check_limit_time_flag`（TIM3）：截止 ≤ now → `Floor_AuthCheck_limit_off`，清 RAM + EEPROM |


限层位图与权限里的「尾 5 字节楼层」是不同用途：前者是限时全开策略，后者多来自通行授权（见 `floor_ctrl.c`）。

---

## 八、回写触发一览


| 事件               | 函数（典型）                                           | EEPROM 区       |
| ---------------- | ------------------------------------------------ | -------------- |
| 上电读配置            | `ReadKey`                                        | 读 `3754..4095` |
| 上电读黑名单           | `loadDataFromEEPROM`                             | 读 `0..2199`    |
| AT 配网成功          | `Cmd_Setting_OnAtSequenceDone`                   | 写设备+MQTT 等     |
| 无 MQTT 的 Setting | `Cmd_Setting_WriteKeysToEeprom_sim`              | 写设备配置段         |
| 限层命令             | `Cmd_Set_OpenLimit`                              | 写 `3758..3776` |
| 限层到期             | `main` + `WriteRawBytes`                         | 清限层区           |
| 云端黑名单 ±          | `addDataToBlacklist` / `deleteDataFromBlacklist` | 写黑名单槽          |
| 本地写公钥            | `SetDevicePublicKey`                             | 写 `3826..3841` |


---

## 九、源码入口速查


| 想搞清楚…                | 先打开                                    |
| -------------------- | -------------------------------------- |
| 地址宏 / 容量             | `Hardware/Storage/eeprom.h`            |
| 读写封装                 | `Hardware/Storage/eeprom.c`、`24cxx.c`  |
| 黑名单                  | `App/Store/blackList.c`                |
| 全局配置与验签辅助            | `App/Pass/data_handler.c`              |
| Setting / 限层 / 权限时间窗 | `App/Pass/cmd.c`                       |
| 上电顺序                 | `User/board_init.c`、`User/app_run.c`   |
| 限层到期                 | `User/main.c`（`check_limit_time_flag`） |
| RTC                  | `Hardware/Time/RTC.c`、`ds1302.c`       |


---

## 十、阶段 E 联调检查清单

1. USART1 启动日志：`ReadKey` 相关打印、`blacklist:` 装载、`floors limit time:`。
2. 空片或全 `0xFF`：设备账号/MQTT 为空 → AT / 验签会失败。
3. RTC：先看打印时间是否合理；`year < 26` 会走默认时间。
4. 通行失败：黑名单（RAM）→ 签名/公钥 → 时间窗/漂移 → 设备类型分支。
5. 改完 MQTT Setting：须等 AT 序列成功并复位后，才算真正写入 EEPROM。
6. 改公钥后若重启丢失：查是否走了只改 RAM 的云端分支。

---

## 十一、已知缺口（读代码时别踩）

- `eeprom.md` 与本文分工：流程看本文，地址/API 看 `eeprom.md`（以 `eeprom.h` 为准）。
- `EEPROM_WritePage` 在头文件声明，现实现可能未提供；黑名单多为按字节写。
- 云端 case 107 改公钥可能不落盘。
- 读头黑名单命令现路径不可达。
- `SetDeviceCredentials` 等若仅有声明无实现，勿当已有 API。

