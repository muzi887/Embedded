# EEPROM 说明（本工程）

> **相关文档**
>
> - [docs/README.md](../README.md) — 文档索引
> - [project-overview.md](../overview/project-overview.md) — 黑名单 / 密钥在系统中的位置
> - [onboarding-f103.md](../overview/onboarding-f103.md) — 阶段 E：存储与联调
> - Embedded：[STM32-I2C说明.md](../../../docs/STM32-I2C说明.md) — 开漏、从机地址、指定地址读写、Sr

更新时间：2026-07-17

---

## 一、EPROM 还是 EEPROM？

口语和旧注释里常写成 **EPROM**，本工程实际用的是 **EEPROM**：

| 名称 | 全称 | 特点 | 本工程？ |
|------|------|------|----------|
| **EPROM** | Erasable Programmable ROM | 紫外线擦除，整片擦写 | 否 |
| **EEPROM** | Electrically Erasable PROM | **电擦除**，按字节/页改写 | **是** |
| **Flash** | — | 块擦除，MCU 程序区常见 | 程序在片内 Flash，业务数据不落这里 |

源码路径、函数名也是 `eeprom`（如 `HARDWARE/eeprom.c`）。注释里的 `eprom` / `add_epromdata` 指同一块芯片。

**记忆**：掉电要留的配置 → **EEPROM**；本工程经 **软件 I2C** 读写外挂芯片。

---

## 二、在梯控里干什么

掉电后仍要保留的数据放 EEPROM，上电再装回 RAM：

| 数据 | 用途 | 主要入口 |
|------|------|----------|
| **黑名单** | 通行前拦截 UUID | `addDataToBlacklist` / `deleteDataFromBlacklist` / `loadDataFromEEPROM` |
| **设备公钥** | 扫码命令 1～3 验签 | `WriteKey` / `ReadKey` / `SetDevicePublicKey` |

主流程（`User/main.c`）：

```text
IIC_Init()           // PE2/PE3 软件 I2C，挂 RTC + EEPROM
    ↓
ReadKey()            // 从地址 PUBLICKEY_ADR 读 8 字节公钥
    ↓
loadDataFromEEPROM() // 从地址 0 起装载最多 200 条黑名单到 dataArray[]
    ↓
主循环（扫码 / 4G 等业务里再增删黑名单、改公钥）
```

与 RTC（DS3231）**共用同一条软件 I2C 总线**，靠不同从机地址区分。

---

## 三、硬件与总线

### 3.1 接线（本工程）

| 信号 | MCU 引脚 | 说明 |
|------|----------|------|
| **SCL** | **PE2** | `IIC_SCL`（`HARDWARE/iic.h`） |
| **SDA** | **PE3** | `IIC_SDA` |
| VCC / GND | 板级供电 | 与 MCU 共地 |

驱动是 **GPIO 软件模拟 I2C**（`Hardware/Storage/iic.c`），不是硬件 `I2C1/I2C2`。时序概念与 [STM32-I2C说明.md](../../../docs/STM32-I2C说明.md) 相同。

### 3.2 从机地址与字地址

```c
// HARDWARE/eeprom.h
#define EEPROM_DEVICE_ADDR 0x57   // 7 位从机地址
#define PUBLICKEY_ADR      4000   // 公钥起始字地址
```

| 项 | 本工程取值 | 说明 |
|----|------------|------|
| **7 位从机地址** | `0x57` | 写出首字节：`(0x57<<1)=0xAE`（写）、`0xAF`（读） |
| **字地址宽度** | **16 位**（高字节 + 低字节） | 见 `EEPROM_WriteByte` / `EEPROM_ReadByte` |
| **容量暗示** | 公钥在 **4000** | 至少约 4 KB → 常见 **AT24C32**（32 Kbit）一类；头文件注释写 AT24C02 易误导（C02 仅 256 B、8 位字地址） |

未写过的单元上电读数多为 `0xFF`；`ReadKey` 用首字节是否为 `0xFF` 判断「是否已存过密钥」。

---

## 四、地址布局

```text
EEPROM 字地址（字节编址）
┌──────────────────────────────────────────────┐
│ 0 ～ 2199                                    │
│   黑名单：最多 200 条 × 11 字节              │
│   槽 i 起始 = i * 11                         │
├──────────────────────────────────────────────┤
│ 2200 ～ 3999                                 │
│   预留（代码未划业务区）                     │
├──────────────────────────────────────────────┤
│ 4000 ～ 4007  PUBLICKEY_ADR                  │
│   设备公钥 8 字节                            │
└──────────────────────────────────────────────┘
```

### 4.1 黑名单一条 11 字节

定义见 `HARDWARE/blackList.h`：`DATA_SIZE = 11`，`MAX_ARRAY_SIZE = 200`。

| 偏移 | 内容 | 约定 |
|------|------|------|
| 0～9 | UUID / ID 数据（最多 10 字节） | 与内存 `dataArray[i].data` 对应 |
| **10** | **空闲标记** | `0x00` = 有效；`0x01` = 空（删除时写这里） |

- **添加**：找空槽 → 写满 11 字节，末字节置 `0`  
- **删除**：只把该槽第 11 字节写成 `0x01`（不整槽擦 `0xFF`）  
- **加载**：`data[10]==0x00` 才拷进 RAM 并打印

通行拦截读的是 **RAM 中的 `dataArray`**；改黑名单后业务侧常再调 `loadDataFromEEPROM()` 做同步。

### 4.2 公钥

| API | 行为 |
|-----|------|
| `WriteKey(pk)` | 向 `PUBLICKEY_ADR+0…7` 写 8 字节 |
| `ReadKey()` | 若 `EEPROM_ReadByte(PUBLICKEY_ADR)!=0xFF`，读 8 字节到 `g_device_publicKey` |
| `SetDevicePublicKey()` | 先更新 RAM，再 `WriteKey` |

---

## 五、驱动 API（`HARDWARE/eeprom.c`）

| 函数 | 作用 |
|------|------|
| `EEPROM_WriteByte(addr, data)` | 写 1 字节；STOP 后 `delay_ms(5)` 等内部写周期 |
| `EEPROM_WritePage(addr, buf, len)` | 连续写多字节（仍一次 STOP + 5 ms）；跨页风险由调用方负责 |
| `EEPROM_ReadByte(addr)` | 假写设字地址 → **Sr** → 读 1 字节（NACK） |
| `EEPROM_ReadBuffer(addr, buf, len)` | 同上，多字节读；末字节 NACK |
| `WriteKey` / `ReadKey` | 公钥专用封装 |

读写时序骨架（与 I2C 专文「指定地址读」一致）：

```text
写一字节:
  S | 从机写地址 | ACK | 字地址高 | ACK | 字地址低 | ACK | data | ACK | P
  → 等待约 5 ms（芯片内部编程）

读一字节:
  S | 从机写地址 | ACK | 字地址高 | ACK | 字地址低 | ACK
  Sr| 从机读地址 | ACK | data | NACK | P
```

上层黑名单当前多用 **逐字节 `EEPROM_WriteByte`**，可靠但较慢；页写适合以后优化，但要注意 **页边界**（常见 32 字节/页，跨页会回绕或写坏）。

---

## 六、和业务的衔接

| 场景 | 典型调用链 |
|------|------------|
| 上电 | `ReadKey` → `loadDataFromEEPROM` |
| 扫码/串口加黑名单 | `addDataToBlacklist` → EEPROM 槽写入 |
| 云端加/删黑名单 | `parse_addblack_json` / `parse_delblack_json` → 黑名单 API → 常再 `loadDataFromEEPROM` |
| 下发/更新公钥 | `SetDevicePublicKey` → `WriteKey` |
| 整表白名单清空 | `clearPublicKeySpace`（名易误解：实际清的是黑名单槽标记，不是公钥区） |

联调前确认：I2C 上拉与地址正确、`ReadKey` 已打出公钥字符、USART1 能看到 `blacklist:` 加载日志。

---

## 七、注意点与易错处

1. **名称**：要找的是 EEPROM / `eeprom.c`，不是紫外线擦除的 EPROM。  
2. **容量与型号**：代码按 **16 位字地址 + 地址 4000** 写，按 **≥4 KB** 芯片理解；不要按 AT24C02（256 B）接线调库。  
3. **写后延时**：每次 `WriteByte`/`WritePage` 后固定等 5 ms；频繁写黑名单会拖慢主循环。  
4. **空闲标记**：有效靠末字节 `0x00`，空靠 `0x01`；与「空白 = 0xFF」的直觉不同。  
5. **公钥判空**：仅看地址 4000 是否为 `0xFF`；若曾写过再想「清空」，需主动写回 `0xFF` 或改逻辑。  
6. **总线共享**：EEPROM 与 DS3231 同总线，一方挂死（无 ACK）会影响另一方；排障时先查 ACK / 地址。  
7. **`clearPublicKeySpace`**：只把 200 个槽的标记字节写成 `0x01`，**不动** `PUBLICKEY_ADR` 的密钥。

---

## 八、源码索引


| 文件 | 内容 |
|------|------|
| `HARDWARE/eeprom.h` / `eeprom.c` | 从机地址、字地址读写、公私钥 |
| `HARDWARE/blackList.h` / `blackList.c` | 槽布局、增删、加载 |
| `HARDWARE/iic.h` / `iic.c` | PE2/PE3 软件 I2C |
| `HARDWARE/data_handler.c` | `SetDevicePublicKey`、云/串口黑名单解析 |
| `User/main.c` | 上电 `ReadKey` + `loadDataFromEEPROM` |
