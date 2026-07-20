# tikong_jx 命名规则与问题清单

更新时间：2026-07-19  
范围：本工程源码与 `docs/` 文档命名习惯；描述**现行主流做法**，并标出**有问题的命名**。  
相关：`.cursorrules`、[directory-tree.md](./flows/directory-tree.md)

> **原则**：历史代码不强行全库改名；**新增代码**按本文「推荐规则」；改名仅在触碰该 API 时顺带修正。

---

## 1. 现行命名规则（项目实际在用）

### 1.1 目录


| 层级          | 规则             | 例                                              |
| ----------- | -------------- | ---------------------------------------------- |
| 顶层          | PascalCase 英文域 | `User/` `App/` `Hardware/` `System/` `docs/`   |
| App 子域      | PascalCase     | `Pass/` `Cloud/` `Store/` `Link/`              |
| Hardware 子域 | PascalCase     | `Serial/` `Storage/` `Time/` `Board/` `Modem/` |
| docs 主题     | 小写英文           | `overview/` `pass/` `cloud/` `store/`          |


业务 `*Process` 放在 `App/`，**不**放进 `Hardware/Serial/`。

### 1.2 源文件名


| 习惯              | 例                                           | 说明            |
| --------------- | ------------------------------------------- | ------------- |
| 推荐：`snake_case` | `qr_comm.c` `data_handler.c` `floor_ctrl.c` | 新文件优先         |
| 通道入口：`*_comm`   | `g4g_comm.c` `pc_comm.c`                    | 对应 `*Process` |
| 历史例外（保留）        | `Live_data.c` `blackList.c` `RTC.c` `4G.c`  | 勿为风格批量改名      |


### 1.3 函数


| 类别         | 规则                           | 例                                                                |
| ---------- | ---------------------------- | ---------------------------------------------------------------- |
| 对外公开 API   | `PascalCase` 或 `Module_Verb` | `QRProcessUart5` `Floor_CallOne` `EEPROM_ReadByte` `CommControl` |
| 文件内 static | `snake_case`，可加模块前缀          | `qr_handle_password_input` `uart5_find_sub`                      |
| 命令簇        | `Cmd_*`                      | `Cmd_Setting_OnAtSequenceDone`                                   |
| 云/JSON 镜像  | camelCase 可保留                | `parseSerialData` `pwd_Reply`                                    |


> 注意：`.cursorrules` 里「函数一律 camelCase」与树内主流不符；**以本节为准**。

### 1.4 变量与全局


| 类别      | 规则                                                      | 例                                     |
| ------- | ------------------------------------------------------- | ------------------------------------- |
| 局部 / 参数 | `snake_case`                                            | `pwd_len` `received_data_len`         |
| 跨模块全局   | `g`_ + snake                                            | `g_device_code` `g_floors_limit_time` |
| 文件内静态缓冲 | 可选 `s`_                                                 | `s_received_uart5`                    |
| 类型      | 新代码优先 `uint8_t`；Hardware/System 可继续 `u8`/`u16`（`sys.h`） |                                       |


### 1.5 宏与 EEPROM


| 规则      | 例                                          |
| ------- | ------------------------------------------ |
| 地址/长度   | `PUBLIC_<NAME>_ADDR` / `PUBLIC_<NAME>_LEN` |
| RAM 镜像  | `g_<同概念_snake>`                            |
| 新存储 API | `EEPROM_*`；`ReadKey`/`WriteKey` 视为遗留       |


### 1.6 头文件守卫

`__FOO_H` 与 `FOO_H` 并存；**编辑某文件时与该文件原风格一致**。新 User/App 头可用 `FOO_H`。

### 1.7 语言


| 层        | 规则                                         |
| -------- | ------------------------------------------ |
| C 标识符    | **仅英文**（新代码禁止拼音当符号）                        |
| 注释、文档正文  | 中文为主                                       |
| docs 文件名 | 英文 kebab-case 优先：`password-4digit-auth.md` |
| 规格文件     | `YYYY-MM-DD-topic-design.md`（topic 可中文）    |


### 1.8 文档中的协议/符号

专文可对齐符号：`parseSerialData-case101.md`、`CommControl-permission-chain.md`（文件名常小写化符号）。

---

## 2. 有问题的命名（请避免新增同类）

### 2.1 拼写错误


| 现状                | 位置（代表）                       | 问题                | 建议（触碰时再改）                        |
| ----------------- | ---------------------------- | ----------------- | -------------------------------- |
| `CommControl`     | `App/Pass/cmd.h` `.c`        | 缺 **o**（Control）  | `√`                              |
| `AllSeting`       | `App/Pass/cmd.h`             | 缺 **t**（Setting）  | √                                |
| `create_ymdhm`    | `App/Pass/qr_comm.c`         | 缺 **e**（create）   | `√create_ymdhm` 或 `format_ymdhm` |
| `authorize`（文案）   | `qr_comm.c` 等 `g_result.msg` | 应为 **authorized** | √统一英文文案                          |
| `reply` 注释        | `Live_data*.h/.c`            | 本意 **reply**      | √                                |
| `USART4_SendData` | `Hardware/Serial/uart4.h`    | 外设是 **UART4**     | `√UART4_SendData`                |


### 2.2 名实不符 / 过时


| 现状                                     | 位置                    | 问题                           |
| -------------------------------------- | --------------------- | ---------------------------- |
| `create_ymdhm` 旁注释写 `YYYYMMDDhh`       | `qr_comm.c`           | √实际格式含 **分钟** `YYYYMMDDhhmm` |
| EEPROM 调试打印 `g_elevator_tails_limit`_* | `eeprom.c`            | 实际变量是 `g_floors_limit`_*     |
| `onboarding-f103.md`                   | `docs/overview/`      | ×                            |
| `app_comm.h` 中残留 `QRProcess`           | `App/Link/app_comm.h` | 现行入口为 `QRProcessUart4/5`     |
| `clearPublicKeySpace`                  | `blackList.h`         | 名像密钥，却在黑名单模块                 |


### 2.3 同一概念多种写法（不一致）


| 主题         | 冲突例                                                                            | 位置                                   |
| ---------- | ------------------------------------------------------------------------------ | ------------------------------------ |
| 口令         | `pwd_Reply` / `pwd4_value` vs `g_device_password` / `qr_handle_password_input` | Cloud / Pass                         |
| 黑名单        | `BlackUserListAdd` vs `addDataToBlacklist` vs `AddBlacklistIds_FromPacket`     | `cmd` / `blackList` / `data_handler` |
| 楼层 API     | `Floor`** vs `FloorCtrl*`*                                                     | `floor_ctrl.h`                       |
| 继电器        | `Rly_Set` vs `rly_AllOff` vs `rly1out`                                         | `rly.h`                              |
| 蜂鸣         | `Bsp_SetBeep` vs `beepAlarm` vs `Beep_Init`                                    | Board / Pass                         |
| EEPROM API | `EEPROM_ReadByte` vs `WriteKey`/`ReadKey`                                      | `eeprom.h`                           |
| 时间格式化      | `create_ymdhm` / `parseYMDHMS` / `MakeTimestamp14` / `Get_newTime`             | Pass                                 |
| 头文件守卫      | `__QR_COMM_H` vs `DATA_HANDLER_H`                                              | 各 `.h`                               |
| 整型别名       | `u8`/`u16` vs `uint8_t`/`uint16_t`                                             | 全库                                   |


### 2.4 混搭风格（单标识符内）


| 例                                    | 位置               | 问题                                     |
| ------------------------------------ | ---------------- | -------------------------------------- |
| `g_device_public_Key`                | `data_handler.h` | snake 中突然大写 `Key`                      |
| `g_mqtt_productKey` 等                | `data_handler.h` | 前缀 snake + JSON camel（可接受，但需注释「对齐云字段」） |
| `card_Number_uart5`                  | `qr_comm.c`      | snake 中夹 `Number`                      |
| `Floor_AuthCheck_limit`              | `floor_ctrl.h`   | Pascal + snake 后缀                      |
| `Live_data_r`                        | Cloud            | `_r` 含义（receive？）未在文件名说明               |
| `pwd_Reply` vs `call_elevator_Reply` | `Live_data.h`    | 同族有的缩写有的全写                             |


### 2.5 难懂缩写 / 拼音（新人难读）


| 现状                 | 位置                              | 推测含义     | 新代码应写成                        |
| ------------------ | ------------------------------- | -------- | ----------------------------- |
| `chardan`          | `data_handler` `build_frame_A`* | 车单？      | `card_id` / 业务标准名             |
| `chartihao`        | 同上                              | 梯号？      | `elevator_no` / `cabin_id`    |
| `haxi`             | `data_handler.h`                | hash     | `hash` / `digest`             |
| `dachuanzi`        | `Live_data.h`                   | 大串子/载荷   | `payload` / `frame_body`      |
| `Cvj`              | `BuildCvjJson`*                 | 不明缩写     | 全称或注释写清                       |
| `g4g` / `G4G`      | `g4g_comm.c`                    | 4G       | 可保留；注释写清 = 4G 业务              |
| `auth_dic_hash`    | `qr_comm.c`                     | digest？  | `auth_digest`                 |
| `elevator_tail5`   | `floor_ctrl`                    | 5 字节限层位图 | `floor_bitmap5`（改名成本高，注释补全即可） |
| `deviceKey_1`…`_6` | `Live_data.h`                   | 按报文槽编号   | 新缓冲用业务名，避免纯序号                 |


### 2.6 用户可见英文文案质量差

`g_result.msg` 中出现破碎英文（如 `is authorize`、`all gate is all authorized`）。属产品字符串问题，新文案应完整、统一时态。

---

## 3. 新增代码检查清单

写新符号前过一遍：

1. 文件：优先 `snake_case.c/.h`
2. 公开函数：`Module_Verb` 或与邻文件一致的 `PascalCase`
3. static：`snake_case` + 可选模块前缀（如 `qr`_）
4. 全局：`g_` + 全小写 snake；对齐 JSON 的字段可保留 camel 段并注释
5. 禁止新拼音标识符（`haxi`/`chardan` 类）
6. 禁止新拼写错误（Contrl/Seting/creat）
7. 同一文件内同一概念只用一种拼法（`password` 或 `pwd` 选一种对外）
8. EEPROM 新宏跟 `PUBLIC_*_ADDR` 族；调试打印名与变量名一致

---

## 4. 高优先级清理（下次改到再动）

按收益排序，**不要为清理而开大 PR**：

1. `eeprom.c` 调试字符串与 `g_floors_limit_`* 对齐
2. 新 Pass/Cloud 代码停止新增拼音参数名
3. `app_comm.h` 去掉/更正过时的 `QRProcess` 声明

---

## 5. 与 HH-TOTP-4 文档命名的关系

`docs/pass/newpw/` 下设计文档使用英文 kebab：


| 文件                                      | 含义                  |
| --------------------------------------- | ------------------- |
| `password-4digit-design.md`             | 按设备种类叙述的设计          |
| `password-4digit-design-deviceuid.md`   | 统一 `device_id` 版    |
| `password-4digit-eng-unit-ratelimit.md` | 选单元+RateLimit 软硬件落地 |
| `password-4digit-flow-*-simple.md`      | 通俗流程                |


规则：`password-4digit-<主题>.md`；工程实现符号仍按本文 §1（如未来 `qr_handle_visitor_code`），**不要**把文档文件名直接当 C 符号。

---

## 6. 一句话

本工程是 **江协分层目录 + Pascal/`Module_` 公开 API + snake 内部函数 + `g_` 全局 + 云侧 camel/JSON + 中文注释** 的混合体；问题主要在 **拼写错误、拼音标识符、同义多名、名实不符**。新增对齐邻域文件，旧名触碰再改。