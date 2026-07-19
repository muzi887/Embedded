# EEPROM 说明（本工程）

更新时间：2026-07-18

> **相关文档**
>
> - **[storage-logic.md](./storage-logic.md)** — 阶段 E 全链路（上电顺序、回写触发、RTC/限层）；本文只讲 **芯片 / 地址 / API**
> - [onboarding-f103.md](../overview/onboarding-f103.md) — 阶段 E
> - [at-config-flow.md](../cloud/4g/at-config-flow.md) — `Cmd_Setting` 成功后再写配置区
> - Embedded：[STM32-I2C说明.md](../../../docs/STM32-I2C说明.md) — 开漏、从机地址、指定地址读（概念对照）

权威源码：`Hardware/Storage/eeprom.h`、`eeprom.c`、`24cxx.h` / `24cxx.c`。

---

## 一、EPROM 还是 EEPROM？

| 名称 | 全称 | 特点 | 本工程？ |
|------|------|------|----------|
| **EPROM** | Erasable Programmable ROM | 紫外线擦除 | 否 |
| **EEPROM** | Electrically Erasable PROM | **电擦除**，按字节/页改写 | **是**（AT24C32） |
| **Flash** | — | 块擦除 | 程序在片内 Flash；业务数据不落这里 |

口语/旧注释里的 `eprom` 指同一块外挂 EEPROM。

---

## 二、硬件与总线

| 项 | 本工程 |
|----|--------|
| 芯片 | **AT24C32**，容量 **4096** 字节（字地址 `0..4095`） |
| 接口 | 硬件 **I2C1**（非软件 bit-bang） |
| 引脚 | **PB6** SCL、**PB7** SDA |
| 7 位从机地址 | **`0x50`**（`AT24C32_ADDR_7BIT`，A2/A1/A0=0） |
| 页大小 | **32** 字节（`AT24C32_PAGE_SIZE`） |
| 初始化 | `board_init()` → `AT24C32_I2C_Init()` → `ReadKey()` |

```text
eeprom.c  API
    ↓
24cxx.c   AT24C32_Read / AT24C32_Write
    ↓
I2C1 @ PB6/PB7，从机 0x50
```

**不要与 RTC 混总线**：时钟是 **DS1302**（PE9/10/11 三线），见 [storage-logic.md](./storage-logic.md)。  
`Hardware/Storage/iic.c`（PE2/PE3 软件 I2C）**现 init 未调用**。

> `eeprom.h` 里仍有 `#define EEPROM_DEVICE_ADDR 0xA0`，**现读写不走该宏**；以 `24cxx.h` 的 `0x50` 为准。

---

## 三、在梯控里存什么

| 区 | 地址 | 用途 | 上电装载 |
|----|------|------|----------|
| 黑名单 | `0..2199` | ≤200 条 ID | `loadDataFromEEPROM` → `dataArray[]` |
| 保留 | `2200..3753` | 未用 | — |
| 漂移 + 限层 | `3754..3776` | 时间漂移、限层位图、截止时间 | `ReadKey` |
| 设备 + MQTT | `3777..4095` | 账号/公钥/模式、MQTT 四元组 | `ReadKey` |

业务何时回写、谁读 RAM：见 [storage-logic.md](./storage-logic.md)。

---

## 四、地址布局（与 `eeprom.h` 一致）

```text
EEPROM 字地址（字节编址，0..4095）
┌─────────────────────────────────────────────────────────┐
│ 0 ～ 2199                                               │
│   黑名单：200 槽 × 11 字节；槽 i 起始 = i * 11            │
├─────────────────────────────────────────────────────────┤
│ 2200 ～ 3753  保留                                      │
├─────────────────────────────────────────────────────────┤
│ 3754 ～ 3757  PUBLIC_DRIFT_TIME（4，文本）               │
│ 3758 ～ 3762  PUBLIC_FLOORS_LIMIT（5，二进制位图）       │
│ 3763 ～ 3776  PUBLIC_FLOORS_LIMIT_TIME（14，ASCII 时间） │
├─────────────────────────────────────────────────────────┤
│ 3777 ～ 3792  PUBLIC_DEVICE_ID（16）                    │
│ 3793 ～ 3808  PUBLIC_DEVICE_PASSWORD（16）              │
│ 3809          PUBLIC_DEVICE_TYPE（1）                   │
│ 3810 ～ 3825  PUBLIC_DEVICE_CODE（16）                  │
│ 3826 ～ 3841  PUBLIC_KEY（16）                          │
│ 3842 ～ 3871  PUBLIC_DEVICE_MODE（30）                  │
│ 3872 ～ 3903  PUBLIC_MQTT_ADDR（32）                    │
│ 3904 ～ 3967  PUBLIC_MQTT_PRODUCTKEY（64）              │
│ 3968 ～ 4031  PUBLIC_MQTT_DEVICEKEY（64）               │
│ 4032 ～ 4095  PUBLIC_MQTT_DEVICESECRET（64）            │
└─────────────────────────────────────────────────────────┘
```

### 4.1 黑名单槽（11 字节）

定义：`App/Store/blackList.h` — `DATA_SIZE = 11`，`MAX_ARRAY_SIZE = 200`。

| 偏移 | 内容 | 约定 |
|------|------|------|
| 0～9 | ID（最多 10 字节） | 对应 `dataArray[i].data` |
| **10** | **占用标记** | `0x00` = 有效；`0x01` = 空/已删 |

- **添加**：找空槽 → 写满 11 字节（标记 `0x00`）
- **删除**：只把偏移 10 写成 `0x01`（内容字节可残留）
- **加载**：`data[10]==0x00` 才拷进 RAM

通行拦截读的是 **RAM `dataArray`**，不是每次扫 EEPROM。

### 4.2 文本区 vs 限层位图

| 类型 | 读法 | 「空」判定 |
|------|------|-----------|
| 设备/MQTT/漂移/限层时间等文本 | `EEPROM_ReadTextField`（`ReadKey` 内部） | 首字节 **`0xFF`** → 打印 `(empty)`，字符串置空 |
| 限层 5 字节位图 | `EEPROM_ReadBuffer` | **不能**用首字节 `0xFF` 当空（「全开」也可是 `FF FF …`） |

### 4.3 公钥

| 项 | 取值 |
|----|------|
| 地址 | `PUBLIC_KEY_ADDR` = **3826** |
| 长度 | **16**（`PUBLIC_KEY_LEN`） |
| RAM | `g_device_public_Key` |
| 写 | `WriteKey(..., PUBLIC_KEY_ADDR, PUBLIC_KEY_LEN)` 或 `SetDevicePublicKey` |

---

## 五、驱动 API

路径：`Hardware/Storage/eeprom.c`（底层 `24cxx.c`）。

| 函数 | 作用 |
|------|------|
| `EEPROM_WriteByte(addr, data)` | 写 1 字节 → `AT24C32_Write`（内部含写周期等待） |
| `EEPROM_ReadByte(addr)` | 读 1 字节 |
| `EEPROM_ReadBuffer(addr, buf, len, tag)` | 连续读；`tag` 非空时打印十六进制 |
| `WriteKey(str, addr, len)` | 先清零再写可打印字符串；`str==NULL` 只清零 |
| `WriteRawBytes(data, addr, len)` | 写二进制；`data==NULL` 清零；限层位图用此接口 |
| `ReadKey()` | 装载配置区 → `g_device_*` / `g_mqtt_*` / `FloorCtrl_SetLimit` / `g_floors_limit_time` |
| `EEPROM_ClearAllParams()` | 清配置区（含限层）后再 `ReadKey`；**不清黑名单** |

**声明但未实现**：`EEPROM_WritePage`（`eeprom.h` 有原型，`eeprom.c` 无定义）。黑名单等多用逐字节 `EEPROM_WriteByte`。

底层写：`AT24C32_Write` 按 **32 字节页** 切分，跨页由驱动处理；上层仍应避免无意义的超长阻塞写。

---

## 六、和业务的衔接（摘要）

| 场景 | 典型调用 |
|------|----------|
| 上电配置 | `AT24C32_I2C_Init` → `ReadKey`（`board_init`） |
| 上电黑名单 | `loadDataFromEEPROM`（`app_init`） |
| 云端加/删黑名单 | `parse_addblack_json` / `parse_delblack_json` → `addDataToBlacklist` / `deleteDataFromBlacklist` |
| 含 MQTT 的 Setting | AT 成功 → `Cmd_Setting_OnAtSequenceDone` → `WriteKey*` |
| 限层命令 | `Cmd_Set_OpenLimit` → `WriteRawBytes` |
| 清空配置区 | `EEPROM_ClearAllParams`（工具向；现网调用点需自行检索） |
| `clearPublicKeySpace` | **只清黑名单槽标记**，不动公钥区（函数名易误解） |

完整回写表见 [storage-logic.md §八](./storage-logic.md)。

---

## 七、注意点与易错处

1. **型号**：按 **AT24C32 / 16 位字地址 / 4KB** 理解；不要按 AT24C02（256 B）调库。
2. **从机地址**：用 **`0x50`**，不要用头文件里闲置的 `EEPROM_DEVICE_ADDR 0xA0`。
3. **写周期**：频繁逐字节写黑名单会拖慢主循环。
4. **空闲标记**：黑名单有效靠末字节 `0x00`，空靠 `0x01`；与「空白 = 0xFF」不同。
5. **文本判空**：配置文本看首字节是否 `0xFF`；限层位图例外。
6. **RTC 不在这条 I2C 上**：EEPROM 挂死不影响 DS1302 接线，但会影响 `ReadKey` / 黑名单。
7. **路径**：现码在 `Hardware/Storage/`、`App/Store/`，不是旧树的 `HARDWARE/`。

---

## 八、源码索引

| 文件 | 内容 |
|------|------|
| `Hardware/Storage/eeprom.h` / `eeprom.c` | 地址宏、`ReadKey` / `WriteKey` / `WriteRawBytes` |
| `Hardware/Storage/24cxx.h` / `24cxx.c` | I2C1、从机 `0x50`、页写/读 |
| `Hardware/Storage/iic.c` | 遗留软件 I2C（现未进 init） |
| `App/Store/blackList.h` / `blackList.c` | 槽布局、增删、加载 |
| `App/Pass/data_handler.c` | `g_device_*` / `g_mqtt_*`、`SetDevicePublicKey` |
| `User/board_init.c` | `AT24C32_I2C_Init` + `ReadKey` |
| `User/app_run.c` | `loadDataFromEEPROM` |
