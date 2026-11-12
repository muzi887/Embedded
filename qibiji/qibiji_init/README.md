# 下位机初始化配置工具（STM32 + Qt 上位机）

通过串口连接上下位机，完成两件事：

1. 配置下位机 EEPROM 中的 4G / MQTT 参数（productKey、deviceKey、deviceSecret、mqtt_addr）；
2. 配置 4G 模块（WH-LTE-7S1）——一键下发整套 MQTT 参数，并提供 AT 指令控制台。

## 目录

| 位置 | 说明 |
| --- | --- |
| `USER/`、`HARDWARE/`、`SYSTEM/`、`CORE/` | STM32F103VE 下位机固件（Keil MDK 工程 `USER/qibiji_init.uvprojx`，原 `BOOTV10.uvprojx`） |
| `APP/qibiji_init/` | Qt6 Widgets 上位机（`qibiji_init.pro`） |
| `HEXBIN/qibiji_init.bin` | 下位机固件产物（Keil After-Build 自动生成，原 `boot.bin`） |

## 硬件连接

| 下位机 | 连接对象 | 参数 |
| --- | --- | --- |
| USART1（PA9/PA10） | PC / Qt 上位机 | **460800 8N1**，无流控 |
| USART2（PA2/PA3） | WH-LTE-7S1 4G 模块 | 115200 8N1 |
| PA1 | 4G 模块 RST | 复位输出 |
| AT24C02（PB10/PB11，软件 I2C） | — | 参数存储 |

调试 `printf` 也输出在 USART1 上，上位机的帧解析器会自动跳过这些非帧字节。

## 通信协议

### 传输层：SBF-20（近场 20B 二进制帧）

```
偏移 0    SOF    固定 0x55
偏移 1    LEN    DATA 字节数 + 1（含 CRC8），取值 2..17
偏移 2    DATA   FLAGS(1B) + PAYLOAD(0..15B)
偏移 2+n  CRC8   CRC8/Maxim，覆盖 SOF+LEN+DATA（整帧校验结果为 0）

FLAGS = (MORE << 7) | (INDEX & 0x7F)
```

上位机恒为**发起方**，下位机恒为**应答方**：逐对收发，`INDEX` 从 0 连续递增，响应帧 `INDEX` 与请求帧相同；末对双方 `MORE` 均为 0 时会话结束。探活为一次空 opaque 交换，线帧固定 `55 02 00 DF`。

### 应用层：紧凑 UTF-8 JSON

错误码：`0` 成功、`1` 参数校验失败、`2` 忙 / EEPROM 写失败、`3` 未知 op 或字段、`4` JSON 解析失败、`5` AT 超时。

| op | 请求 | 响应 |
| --- | --- | --- |
| `read` | `{"op":"read"}`（= 全部组 `gate`+`ota`+`mqtt`）或 `{"op":"read","configs":["gate","ota","mqtt"]}`，未知组名忽略 | `{"code":0,"params":{"motor_warning_curr_upper":9,"gate_maxheight":2,"gate_maxturns":252,"gate_kaidu":2,"gate_kaidu_count":43,"OTA_flag":2711724449,"OTA_ver":"20260803-2","OTA_size":128,"UPDATE_flag":0,"productKey":"pk001","deviceKey":"dev001","deviceSecret":"d9fb…4e27","mqtt_addr":"82.157.234.123:1883"}}` |
| `write` | `{"op":"write","params":{…}}`，键同 `read`，可只给部分 | `{"code":0}` / `{"code":1,"message":"<键名>"}` / `{"code":2,"message":"eeprom_fail"}` |
| `at` | `{"op":"at","cmd":"usr.cn#AT+CSQ?"}` | `{"code":0,"data":"+CSQ: 25,0\r\nOK\r\n"}` / `{"code":5,"message":"at_timeout"}` |
| `at3` | `{"op":"at3","cmd":"AT+LADDR"}`（USART3 = 蓝牙 DX-BT24） | `{"code":0,"data":"+LADDR=48872D8429CB\r\n"}` / `{"code":5,"message":"at3_timeout"}` |

- 参数分组：`gate`（5 项 u16）、`ota`（`OTA_flag` u32 / `OTA_ver` 字符串 / `UPDATE_flag` u8 可写，`OTA_size` 只读）、`mqtt`（`productKey`/`deviceKey`/`deviceSecret`/`mqtt_addr` 四个 ≤32 字符字符串）。
- **4G/MQTT 四项（productKey/deviceKey/deviceSecret/mqtt_addr）与 reg.h 一致，存本机 EEPROM 的 64/96/128/160**：由 `read`/`write` 承载；上位机可另外经 AT 指令把同一组值下发到 4G 模块（见下）。
- 字符串字段：`OTA_ver` ≤31 字符、MQTT 四项各 ≤32 字符，只允许可打印 ASCII（不含 `"`、`\`）；留空表示清空该字段。数值字段受存储宽度限制（u16/u8/u32），**不做业务范围校验**；不合法返回 `code=1`，`message` 为键名。
- 闸门 5 项按字写（字节 0x0004/06/08/0A/0C，低字节在前）；OTA 三项按「读 44 字节整块 → 改字段 → 写回」处理，不改动 `OTA_size` 与对齐填充；MQTT 四项各按 32 B 槽整块写。
- `write` 每对超时 5000 ms：一次写满全部字段时下位机最坏要写 10 B(闸门)+44 B(OTA)+128 B(MQTT)，逐字节 10 ms，期间持续喂狗。
- `at` 的应答判定：累积回显里出现 `OK` 或 `ERROR` 就立即返回，否则按"静默 40 ms / 1500 ms 超时"兜底——上位机逐条下发 AT 时依赖这个行为。
- `at3` 与 `at` 同形但走 **USART3**（蓝牙模块）；下位机只是透传，地址/名称/版本的解析都在上位机做。
- `at` 的 `cmd` 最多 128 个字符，下位机自动补 `\r\n`；回显转义后最多返回 256 字节。

### 上位机逐对超时

| 操作 | 每对超时 |
| --- | --- |
| 探活 | 500 ms |
| `read` | 1500 ms |
| `write` | 5000 ms（写 EEPROM 最坏约 1.8 s） |
| `at` | 3000 ms（等模块回显） |

### EEPROM 完整分配表（AT24C02，256 B）

| 区段 | 字节地址 | 长度 | 内容 | 默认值 | 读写方 |
| --- | --- | --- | --- | --- | --- |
| 闸门参数区 | 0x0000–0x0013（0–19） | 20 B | 10 个 u16 小端字 `eeprom_buf[0..9]` | — | 应用工程 + **本固件 R/W（第 2–6 个字）** |
| ↳ `eeprom_buf[0]` | 0x0000 | 2 B | 闸门上限值 gate_upper | — | 代码中已注释，未生效 |
| ↳ `eeprom_buf[1]` | 0x0002 | 2 B | 闸门下限值 gate_lower | — | 代码中已注释，未生效 |
| ↳ `eeprom_buf[2]` | 0x0004 | 2 B | 电机报警电流上限 motor_warning_curr_upper | `10` | 应用工程 + 本固件 |
| ↳ `eeprom_buf[3]` | 0x0006 | 2 B | 闸门全开高度 gate_maxheight（mm） | `1000` | 应用工程 + 本固件 |
| ↳ `eeprom_buf[4]` | 0x0008 | 2 B | 螺纹圈数 gate_maxturns（量程基准） | `80` | 应用工程 + 本固件 |
| ↳ `eeprom_buf[5]` | 0x000A | 2 B | 开度百分比 gate_kaidu | `0` | 应用工程 + 本固件 |
| ↳ `eeprom_buf[6]` | 0x000C | 2 B | 开度计数 gate_kaidu_count | `0` | 应用工程 + 本固件 |
| ↳ `eeprom_buf[7..9]` | 0x000E–0x0013 | 6 B | buf[7]=采样/上报间隔、buf[8]=上报周期（应用工程使用）；buf[9] 未用 | — | 应用工程 |
| 未分配 | 0x0014–0x003F（20–63） | 44 B | 空闲 | — | — |
| MQTT 参数区 | 0x0040–0x00BF（64–191） | 128 B | 4 个 32 B 字符串槽（productKey / deviceKey / deviceSecret / mqtt_addr） | 空（`0xFF`） | 本固件 + 应用工程 R/W |
| 未分配 | 192–199 | 8 B | 空闲 | — | — |
| OTA 信息区 | 200–243 | 44 B | `OTA_InfoCB` 结构体整块读写 | — | 本固件 R/W（`OTA_size` 只读）+ 应用工程 / bootloader |
| ↳ OTA_flag | 200–203 | 4 B | 运行区标志（`0xA2A2A2A2`=A2 有待搬运固件） | `0xA1A1A1A1` | |
| ↳ OTA_ver | 204–235 | 32 B | 固件版本号字符串 | `v0.0.0` | |
| ↳ OTA_size | 236–239 | 4 B | 固件字节数 | `0` | |
| ↳ UPDATE_flag | 240 | 1 B | 0=不联网升级 / 1=需联网 OTA | `1` | |
| ↳ 对齐填充 | 241–243 | 3 B | 结构体对齐补零 | `0` | |
| 未分配 | 244–249 | 6 B | 空闲 | — | — |
| 存在性标记 | 250 | 1 B | `0x55`，`AT24CXX_Check()` 上电检测位 | `0x55` | 本固件 + 应用工程 + bootloader |
| 未分配 | 251–255 | 5 B | 空闲 | — | — |

约定与坑：

- `AT24CXX_WriteWord(addr, data)` 的 `addr` 是**字节地址**（低字节在 `addr`，高字节在 `addr+1`），字序号 = 字节地址 ÷ 2。
- 字节 0–19 用 `AT24CXX_Readdatas(0x0000, eeprom_buf, 10)` 整块读；闸门 5 项按**字**写；OTA 区按 44 B 整块读写（改任一字段都要重写整块 200–243）。
- 64–191 是 MQTT 参数区，地址与 `qibiji/HARDWARE/REG/reg.h` 的 `MQTT_*_BYTE_ADDR`（64/96/128/160）一致；本固件的 `mqtt` 组按 32 B 槽整块读写。空片首字节 `0xFF` 读回空串，写入空串则整槽清零。
- 本固件**不再擦除** 64–191：上电只做 `AT24CXX_Check()`（250 标记），不会触碰 MQTT 槽，也不再有 `mqtt area 64-191` 相关打印。
- `PROP_REPORT_MIN_INTERVAL_MS_ADDR(30)` / `PROP_OPENING_THRESHOLD_ADDR(31)` 是 RAM 寄存器表下标，与 EEPROM 无关；应用工程 `reg.h` 中已删除（无引用）。
- 默认值已统一为：`motor_warning_curr_upper=10`、`gate_maxheight=1000`、`gate_maxturns=80`、`OTA_ver="v0.0.0"`、`OTA_flag=0xA1A1A1A1`、`UPDATE_flag=1`。
- 驱动逐字节写 + `delay_ms(10)`：写满 10 B 闸门 + 44 B OTA + 128 B MQTT 约 1.8 s，都在 3.18 s 看门狗内（各段之间喂狗）。

### 4G 配置（上位机逐条下发，下位机只做 AT 透传）

上位机按 **「参数配置」页 MQTT 组的 4 个参数（productKey / deviceKey / deviceSecret / mqtt_addr，来自下位机 EEPROM）+ 本页 2 个可选的重连输入框**（重连间隔 / 重连次数）生成下面的指令，逐条经 `{"op":"at","cmd":…}` 透传给模块，每条要求回显含 `OK` 且不含 `ERROR`：

```
AT+CSQ? / AT+WKMOD=MQTT,NOR / AT+MQTTSVR=<host,port> / AT+MQTTUSER=<deviceKey>
AT+MQTTPSW=<deviceSecret> / AT+MQTTCID=<deviceKey> / AT+MQTTMOD=1 / AT+HEARTEN=OFF
[AT+SOCKRSTIM=<重连间隔> / AT+SOCKRSNUM=<重连次数>]   ← 两个框留空则这两条不下发
AT+MQTTPUBTP=1..8  → iot/v1/<productKey>/<deviceKey>/up/…
AT+MQTTSUBTP=1..5  → iot/v1/<productKey>/<deviceKey>/down/…
AT+S
```

`AT+S` 会让模块保存配置并自行重启，因此不再额外拉 PA1 复位；该条允许"超时但无 ERROR"视为通过（模块保存期间可能不回复）。
**重连策略（防"配置错就反复重启"，可选）**：两个输入框**默认留空且不做本地记忆**，留空 = 本次不下发（模块保持原值）。
填了才发送，真机实测两者有效范围 1–65535（`0` 与 `100000` 均被拒 `+CME ERROR:53`），所以"不停重试"只能用最大次数表达：
建议 `SOCKRSTIM=30` + `SOCKRSNUM=65535`（30 s × 65535 ≈ 22 天才可能重启一次，远优于出厂的 5 次 × 60 s ≈ 5 分钟一轮）。
这两条必须在 `AT+S` 之前下发才会被保存；命令行工具 `tools/sbf20_cli.ps1` 的 `-SockRstim/-SockRsnum` 默认值为 0，同样是"不下发"。
模块被 MCU 复位后需要约 10 s 才应答 AT，因此上位机对每条指令失败会**自动重试 2 次**（间隔 800 ms）。
四项任一为空 / 超 32 字符、`mqtt_addr` 缺端口分隔符、或生成的某条指令超过 128 字符时，上位机本地拦截，不下发。
「全部读取」依次发 `AT+MQTTSVR?` / `AT+MQTTUSER?` / `AT+MQTTPSW?` / `AT+MQTTCID?` / `AT+SOCKRSTIM?` / `AT+SOCKRSNUM?` 共 6 条，解析 `+TAG:值` 回填对应输入框（前四项在「参数配置」页 MQTT 组）；`productKey` 在模块里没有对应项（只体现在 topic 中），保持本机输入。要把模块里的值也写进下位机 EEPROM，读取后回「参数配置」页点「写入参数」。
注意 `AT+S` 会让模块保存并重启，重启期间（约 10 s）AT 查询无响应——一键配置成功后等约 10 秒再点「全部读取」复核（界面与日志里已给出提示）。

## 编译

### 蓝牙模块（DX-BT24）

| 项 | 值 |
| --- | --- |
| 接线 | USART3 **全重映射** → **PD8 = TX、PD9 = RX**，9600 8N1，3.3 V |
| 读取 MAC | `AT+LADDR` → 例如 `+LADDR=48872D8429CB`（12 位无分隔，上位机显示为 `48:87:2D:84:29:CB`） |
| 读取名称 | `AT+NAME` → `+NAME=BT24` |
| 读取版本 | `AT+VERSION` → `+VERSION=V2.3.1` |
| 控制台 | 任意 `AT…` 经 `{"op":"at3","cmd":…}` 透传，原始回显原样返回 |

上位机「蓝牙」页：三个只读显示框（地址/名称/版本）+「读取蓝牙地址 / 读取蓝牙名 / 读取版本 / 清空日志」+ AT 控制台 + 日志区。
命令行：`-Action bleaddr`（读 MAC 并解析）、`-Action at3 -AtCommand "AT+NAME"`。

> 模块**只在蓝牙未被连接时**才进 AT 模式：手机正连着时读取会超时（界面上会提示"可能手机正连着"）。若一直超时，检查模块是否装好与 PD8/PD9 接线。
> 本机实测（2026-10-09）：`AT`→`OK`、`AT+LADDR`→`48:87:2D:84:29:CB`、`AT+NAME`→`BT24`、`AT+VERSION`→`V2.3.1`。

## 编译

### 下位机（Keil MDK，命令行）

```powershell
& 'D:\Keil5\UV4\UV4.exe' -r USER\qibiji_init.uvprojx -j0 -o build.log
```

成功标准：`0 Error(s), 0 Warning(s)`，并生成 `HEXBIN/qibiji_init.bin`。

### 上位机（Qt 6 + MinGW）

```powershell
$env:Path = 'C:\Qt\6.11.2\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;' + $env:Path
mkdir APP\qibiji_init\build_manual
cd APP\qibiji_init\build_manual
qmake ..\qibiji_init.pro
mingw32-make -j4
```

也可以在 Qt Creator 中打开 `APP/qibiji_init/qibiji_init.pro` 直接构建运行。

## 硬件联调清单

1. 连接 USART1 到 PC，打开上位机 → 「串口连接」选择端口 → 波特率 460800 → 「打开串口」。
2. 点「链路探活」，状态栏应显示探活成功（下位机回 `55 02 00 DF`）。
3. 「参数配置」→「读取参数」，应回填 13 项（闸门 5 项 + OTA 4 项 + MQTT 4 项，含只读 `OTA_size`）。
4. 修改闸门/OTA 中任意一项后「写入参数」：只有改动过的字段会被下发，返回 `code=0` 即成功（回读一次确认）。
5. 「AT 控制台」发送 `usr.cn#AT+CSQ?`（WH-LTE-7S1 的 AT 指令需要 `usr.cn#` 前缀，快捷按钮已带），应看到模块原始回显（含 `+CSQ:` 与 `OK`）。
6. 「4G 配置」→ 填重连间隔/次数（例如 30 / 65535）→ 「全部读取」：应从模块读回 `MQTTSVR / MQTTUSER / MQTTPSW / MQTTCID / SOCKRSTIM / SOCKRSNUM` 六项，前四项回填到「参数配置」页 MQTT 组（`productKey` 保持本机输入）。
7. 「4G 配置」→「全部下发（一键配置）」：进度条从 0 走到 22，逐条显示 OK，最终状态「完成」；`AT+S` 后模块自行重启。
8. 断电重上电后：「读取参数」的 13 项应保持不变（含 MQTT 四项，EEPROM 掉电保持）。
9. 异常场景：不接 4G 模块时发 AT 指令应提示超时（code=5）；填错 `mqtt_addr`（缺端口）时点「全部下发」应在本地被拦截；模块未插 SIM 卡时一键配置会在某条失败并显示该条与原始回显。
10. 「蓝牙」页：**手机蓝牙先断开** → 点「读取蓝牙地址」，应显示 `48:87:2D:84:29:CB` 之类的地址（与手机扫描到的该模块地址一致）；再点「读取蓝牙名」（`BT24`）与「读取版本」（`V2.3.1`）；也可以在下方的 AT 控制台手工发 `AT+LADDR` 看原始回显。

## 说明与限制

- 本固件为**纯配置固件**：不包含联网 OTA 与 A1 区跳转逻辑；链接区域为整片 512 KB Flash。
- USART1 固定 460800：上位机波特率下拉框默认 460800，其它值仅用于排查接线问题。
- 「写入参数」改下位机 EEPROM（闸门/OTA/MQTT）；把参数下发到 4G 模块是独立动作，在「4G 配置」页手动触发。
- MQTT 四项存下位机 EEPROM（64/96/128/160，与 `reg.h` 一致）：换电脑后点「读取参数」即可从设备读回，也可在「4G 配置」页从模块读回。
- 不含业务参数（上报间隔、闸门量等）、蓝牙与鉴权功能。

## 联调工具与记录

### 命令行联调工具

`tools/sbf20_cli.ps1` 用 PowerShell 直接实现 SBF-20 发起方，无需 Qt 即可联调：

```powershell
powershell -ExecutionPolicy Bypass -File tools\sbf20_cli.ps1 -Action probe
powershell -ExecutionPolicy Bypass -File tools\sbf20_cli.ps1 -Action read
powershell -ExecutionPolicy Bypass -File tools\sbf20_cli.ps1 -Action readall
powershell -ExecutionPolicy Bypass -File tools\sbf20_cli.ps1 -Action write -GateMaxheight 1000 -GateMaxturns 80 -MotorWarningCurrUpper 10 -GateKaiduCount 0 -OtaVer v1.0.0
powershell -ExecutionPolicy Bypass -File tools\sbf20_cli.ps1 -Action paramtest                                   # 闸门/OTA 原值回写并比对
powershell -ExecutionPolicy Bypass -File tools\sbf20_cli.ps1 -Action at -AtCommand "usr.cn#AT+CSQ?"
powershell -ExecutionPolicy Bypass -File tools\sbf20_cli.ps1 -Action readmqtt
powershell -ExecutionPolicy Bypass -File tools\sbf20_cli.ps1 -Action config4g -ProductKey pk001 -DeviceKey dev001 -DeviceSecret xxx -MqttAddr 82.157.234.123:1883
powershell -ExecutionPolicy Bypass -File tools\sbf20_cli.ps1 -Action monitor -TimeoutMs 3000     # 原始串口监视
powershell -ExecutionPolicy Bypass -File tools\sbf20_cli.ps1 -Action loopback                    # 短接 TX/RX 自环测试
powershell -ExecutionPolicy Bypass -File tools\sbf20_cli.ps1 -Action probe -SweepBaud            # 波特率扫描
```

`APP/qibiji_init/tests/sessiontest.pro` 是无界面的上位机协议栈自测，直接驱动 GUI 使用的
`DeviceSession` 类，跑 **读全部(9 项) → 备份原值 → 改写闸门与 OTA → 回读比对 → 恢复原值 → 再回读确认
→ 只读回读 4G 模块的 MQTT 参数**，对已配置过的设备是安全的（EEPROM 测完会原样写回，且从不重配 4G 模块）：

```powershell
cd APP\qibiji_init\tests\build
qmake ..\sessiontest.pro && mingw32-make -j4
release\sessiontest.exe COM10
```

### 本机联调结果（COM10，2026-10-07）

下位机固件经 ST-Link 烧写（`Erase Done / Programming Done / Verify OK`），实测通过：

| 用例 | 结果 |
| --- | --- |
| 链路探活 | OK（下位机回 `55 02 00 DF`） |
| 读取参数 | OK，回读 4 项 MQTT（当时还存 EEPROM） |
| 写入参数 + 回读校验 | OK，EEPROM 掉电保持 |
| AT 控制台 `usr.cn#AT+CSQ?` | OK，模块回 `+CSQ: 16,99 … OK` |
| 一键配置 4G（旧版：下位机侧） | OK，22/22 条指令全部成功，模块复位，`state=ok` |
| 读取全部 EEPROM 参数 | OK，13 项字段齐全（当时含 MQTT 4；现为 9 项） |
| 写入闸门/OTA 参数并回读 | OK，`sessiontest` 全流程通过并原样恢复 |
| 一次写满 13 项 | OK，耗时 2.7 s（EEPROM 逐字节 10 ms），看门狗未触发、设备未重启 |
| 越界/类型错误 | OK，`70000`（超 u16）与 `"abc"`（字符串给数值字段）均返回 `{"code":1,"message":"gate_maxheight"}` |
| 复位后保持 | OK，重新烧写复位后 13 项数值不变 |

#### 2026-10-09 变更（4G/MQTT 去 EEPROM 化）

- 下位机：删除 `configure4g` / `status` 两个 op 与 `at_4g_sequence.c/.h`；`read`/`write` 只剩闸门 + OTA（9 项）；`at` 改为"回显里出现 `OK`/`ERROR` 立即返回"，以支撑上位机逐条下发。
- 上位机：新增「4G 配置」页（MQTT 四项 + 重连两项输入、全部下发、全部读取、停止、清空日志、逐条日志、进度），一键配置改由上位机生成 24 条指令并逐条经 `at` 透传下发；`DeviceSession` 移除 `configure4g`/`requestStatus`。「AT 控制台」页也补了「清空日志」。
- 下位机收帧竞态修复：`sbf20_poll()` 改为在临界区取快照、先清标志再解析（原先会在处理期间丢掉紧跟其后的帧）。
- 新增「蓝牙」页与 `at3` 透传：下位机启用 **USART3（PD8/PD9 全重映射，9600）** 与 DX-BT24 通信（`uart3.c` 修好 NVIC 分组、去掉未定义的 `RS485_CE3_*`、加接收累积缓冲），上位机可读蓝牙地址/名称/版本并带 AT 控制台。本机实测：`AT`→`OK`、`AT+LADDR`→`+LADDR=48872D8429CB`（显示 `48:87:2D:84:29:CB`）、`AT+NAME`→`BT24`、`AT+VERSION`→`V2.3.1`。
- 一键配置增加两条**防反复重启**参数（放到 `AT+S` 之前保存）：`AT+SOCKRSTIM`/`AT+SOCKRSNUM`；真机实测有效范围 1–65535（`0`/`100000` 返回 `+CME ERROR:53`）。这两项在「4G 配置」页做成**可编辑输入框**，**默认留空、不做本地记忆**，留空即本次不下发（保持模块原值），填了才校验 1–65535；每条指令失败自动重试 2 次（模块复位后约需 10 s 才应答）。CLI 的 `config4g` 提供 `-SockRstim/-SockRsnum`，默认 0 = 同样不下发。
- 编译：Keil `0 Error(s), 0 Warning(s)`（Code 33392 / ZI 24088，比上一版小 4 KB）；Qt `0 warning`。
- 真机复测：本机 CH340 已重枚举到 **COM5** 且 8 种波特率全静默，ST-Link 下载报 `Target DLL has been cancelled`——下位机当前不可达（疑似未供电，或 SWD/串口都已断开），因此**上述新流程的真机复测待硬件恢复后进行**（联调清单第 1–9 步）。

#### 2026-10-13 变更（MQTT 参数回到 EEPROM，与 reg.h 对齐）

- 以 `qibiji/HARDWARE/REG/reg.h` 的 MQTT 分配（64/96/128/160，各 32 B）为准，撤销上一节的“清除 MQTT 参数区”。
- 下位机：`reg.h` 重新加入 `MQTT_*_BYTE_ADDR`（64/96/128/160）+ `MQTT_KEY_EEPROM_LEN(32)` / `MQTT_KEY_STR_LEN(33)`；`reg.c` 删除 `AT24CXX_ClearMqttArea()`，`USER/main.c` 去掉调用（上电不再擦除、不再打印 `mqtt area 64-191`）；`boot.h` 的 `OTA_ADDR` 改名 `OTA_BYTE_ADDR`（同步 `boot.c`/`gprs_rx.c`/`reg.c`/`sbf20_app.c`）。
- 下位机协议：`sbf20_app.c` 新增 `mqtt` 组（并入默认全组），`read` 返回 `productKey`/`deviceKey`/`deviceSecret`/`mqtt_addr`（空片 `0xFF` 首字节→空串，输出前清洗），`write` 接受四项 ≤32 字符字符串并按 32 B 槽写入（空串清零）。
- 上位机：「参数配置」页新增 MQTT 组（四个输入框，随“读取/写入参数”走 EEPROM）；「4G 配置」页只保留重连两项 + 一键下发/全部读取/停止/清日志，一键下发改用参数页的 MQTT 值生成 24 条 AT。
- 工具：`tools/sbf20_cli.ps1` 的 `write` 支持 `-ProductKey/-DeviceKey/-DeviceSecret/-MqttAddr`，`read`/`readall`/`paramtest` 纳入 `mqtt` 组；`tests/main.cpp`（sessiontest）往返覆盖闸门 + OTA + MQTT，测完原样还原。
- 应用工程 `qibiji`（仅地址统一，不改流程）：`eepromLength 11→10`；`GATE_*_ADDR`→`GATE_*_WORD_IDX`、`MQTT_*_ADDR`→`MQTT_*_BYTE_ADDR`、`OTA_ADDR`→`OTA_BYTE_ADDR`；删除无引用的 `PROP_*_ADDR`；默认值统一为 `10 / 1000 / 80 / "v0.0.0" / 0xA1A1A1A1 / 1`。
- 待办：编译 + 真机复测（联调清单第 1–9 步；`sessiontest` 需带 COM 口运行）。

#### 2026-10-09 变更（彻底清除 MQTT 参数区；已于 2026-10-13 撤销）

- 下位机：`reg.h` 删除 `MQTT_PRODUCTKEY_ADDR`/`MQTT_DEVICEKEY_ADDR`/`MQTT_DEVICESECRET_ADDR`/`MQTT_MQTTADDR_ADDR`/`MQTT_KEY_EEPROM_LEN`/`MQTT_KEY_STR_LEN` 六个宏（已无引用），64–191 从"保留"改为**未分配**，EEPROM 分配表相应更新。
- 新增 `AT24CXX_ClearMqttArea()`（`reg.c`）：整块读 64–191，发现任何非 `0xFF` 就写回 `0xFF`；`main()` 在 `AT24CXX_Check()` 之后、`IWDG_Init()` 之前调用。幂等——擦干净后每次上电只读不写。
- 真机验证（COM5 @460800）：① 临时写入 `0xA5` 码型后上电，串口打印 `mqtt area 64-191 erased` → 擦除逻辑确实在写；② 移除测试代码后重新烧写，上电打印 `mqtt area 64-191 already blank` → 区域已全 `0xFF`；③ `readall` 回归 9 项闸门/OTA 参数原值不变（`9 / 2 / 252 / 2 / 43 / 0xA1A1A1A1 / 20260803-2 / 128 / 0`）。
- 编译：Keil `0 Error(s), 0 Warning(s)`（Code 34376 / RO 564 / RW 152 / ZI 24984），`HEXBIN/boot.bin` 已重新生成。
- 未改动 EEPROM 其他区域：闸门字、OTA 块、`250` 存在性标记、20–63 / 192–199 / 244–249 / 251–255 空闲区均不受影响，代码里也没有清除 192–199 的行为。

#### 2026-10-09 变更（Keil 工程改名 qibiji_init）

- `USER/BOOTV10.uvprojx` → **`USER/qibiji_init.uvprojx`**，配套的 `qibiji_init.uvoptx`（下载器/调试设置，含 ST-Link 序列号）与 `qibiji_init.uvguix.Administrator`（窗口布局）同步改名；Keil 靠文件名配对，三个必须一致。
- `<OutputName>` 由 `boot` 改为 `qibiji_init`（中途有人把它改成 `init`，已统一为工程名），因此产物变为 `OBJ/qibiji_init.axf|.hex|.map` 与 **`HEXBIN/qibiji_init.bin|.hex`**；`HEXBIN.bat` 走 `$K !L @L $L` 参数，无需修改。
- 新旧固件**逐字节相同**（SHA256 `893D3858…85F21C`，35092 B），改名不影响功能。
- 旧名产物（`OBJ/boot.*`、`HEXBIN/boot.*`、`USER/boot.map`、`BOOTV10_Target 1.dep`、两个旧 `.uvguix`）已移到 `_stale_boot_artifacts/`，没有删除，确认无用后可整目录删掉。
- 编译：Keil `0 Error(s), 0 Warning(s)`（Code 34376 / RO 564 / RW 152 / ZI 24984）。
- 改名后重新烧写时下位机不可达：ST-Link 报 `Target DLL has been cancelled`，COM5 探活 0 字节、无上电打印（板子疑似未供电或 SWD/串口连线被碰掉）。**改名本身已由编译验证，烧写验证待硬件恢复后补做。**

> 注意：只有 COM10（CH340）与下位机 USART1 的 **TX↔PA10、RX↔PA9、GND** 都接好时链路才通；
> 排查时可用 `-Action monitor` 看下位机的 `printf` 调试输出（波特率 460800）。
