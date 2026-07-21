# 74HC595 IO 扩展模块（本工程）

更新时间：2026-07-21

> **芯片驱动**：`Hardware/Board/bsp_hc595.c` / `bsp_hc595.h`  
> **逻辑 IO 层**：`Hardware/Board/ext_io.c` / `ext_io.h`  
> **业务使用**：`Hardware/Board/floor_ctrl.c`（楼层输出 1～40）  
> **上电**：`User/board_init.c` → `HC595_Init()` → `ExtIO_Init()`

---

## 1. 一句话

MCU 只有有限 GPIO，梯控要驱动约 **40 路楼层输出**。本板用 **多片 74HC595 级联**，用 4 根线（数据 / 移位时钟 / 锁存 / 输出使能）串行送出多路电平。  
固件分层：**`bsp_hc595` 管时序**，**`ext_io` 管 1～40 号 IO 镜像**，**`floor_ctrl` 管「第几层拉低/拉高」**。

---

## 2. 74HC595 是什么（芯片本身）

74HC595 是 **8 位串入并出移位寄存器**：

| 引脚（常用名） | 作用 |
|----------------|------|
| SER（DS） | 串行数据入 |
| SRCLK（SHCP） | 移位时钟：每来一个边沿移入 1 bit |
| RCLK（STCP） | 锁存：把移位寄存器内容打到输出锁存器 |
| OE（/G） | 输出使能，**低电平有效**；高则输出高阻/关断（本板注释为 ZD） |
| Q0～Q7 | 8 路并行输出 |
| Q7' | 级联到下一片的 SER |

级联：上一片的 `Q7'` → 下一片 `SER`，共用 SRCLK / RCLK / OE，即可用同一组线扩展成 16、24、… 路。

本工程注释约定：**8 片 = 64 bit，业务只用前 40 bit（5 个楼层字节）**。

---

## 3. 本板引脚（STM32F4 · GPIOD）

定义见 `bsp_hc595.h`：

| 595 信号 | MCU | 宏 |
|----------|-----|-----|
| SER | PD7 | `HC595_SER_*` |
| SRCLK | PD5 | `HC595_SRCLK_*` |
| RCLK | PD6 | `HC595_RCLK_*` |
| OE（低有效） | PD4 | `HC595_OE_*` |

`OE_ENABLE()` = 拉低 PD4；`OE_DISABLE()` = 拉高 PD4。

---

## 4. 软件分层

```text
权限 / 口令 / 云端呼梯
        │
        ▼
floor_ctrl.c          ExtIO_Set(楼层号, HIGH/LOW) …
        │
        ▼
ext_io.c              io_buf[8] 镜像 → ExtIO_Refresh
        │
        ▼
bsp_hc595.c           ShiftByte / Latch / OutputEnable
        │
        ▼
PD7/PD5/PD6/PD4 → 级联 74HC595 → 楼层驱动电路
```

| 文件 | 职责 |
|------|------|
| `bsp_hc595` | GPIO 初始化；按字节移位（MSB first）；锁存；OE |
| `ext_io` | `EXT_IO_NUM=40`；改 `io_buf` 后整链刷新 |
| `floor_ctrl` | 楼层 1～40 ↔ IO 1～40；呼层/授权/常开/全开全关 |

业务代码应调 `ExtIO_*` / `Floor_*`，不要直接拼 bit 时序（除非改驱动）。

---

## 5. 驱动 API（`bsp_hc595`）

| 函数 | 行为 |
|------|------|
| `HC595_Init` | 配置 PD4/5/6/7 推挽输出；默认 SER/SRCLK/RCLK 低；**OE 关闭**（防上电乱输出） |
| `HC595_OutputEnable(en)` | `en!=0` 开输出，否则关 |
| `HC595_ShiftByte(dat)` | 移入 1 字节，**高位先出**（`dat & 0x80` → SER，再 `dat<<=1`） |
| `HC595_Latch` | RCLK 低→高，锁存到 Q 端 |
| `HC595_WriteBytes` | 连续移位多字节后拉高 RCLK（级联辅助；现网刷新主要走 `ExtIO_Refresh`） |

移位核心：

```48:64:Hardware/Board/bsp_hc595.c
void HC595_ShiftByte(uint8_t dat)
{
    uint8_t i;

    for (i = 0; i < 8; i++)
    {
        SRCLK_LOW();

        if (dat & 0x80)
            SER_HIGH();
        else
            SER_LOW();

        SRCLK_HIGH();
        dat <<= 1;
    }
}
```

---

## 6. 扩展 IO 层（`ext_io`）

### 6.1 缓冲与片序

```text
io_buf[0] → U1（IO1～8，最靠近 MCU）
io_buf[1] → U2（IO9～16）
…
io_buf[7] → U8
业务只用到约前 5 片（40 bit）
```

`ExtIO_Set(index, value)`：`index` 为 **1～40**；内部 `index--` 后 `chip = index/8`，`bit = index%8`。

### 6.2 刷新方向（必读）

级联时，**先移入的字节会跑到链的最远端**。因此刷新必须 **从 U8 到 U1 反向发送**：

```63:78:Hardware/Board/ext_io.c
void ExtIO_Refresh(void)
{
    int i;

    /*
     * 关键点：
     * 595 的 shift 先的数据会跑到最远端
     * 所以必须从 U8 -> U1 反向发送
     */
    for (i = 7; i >= 0; i--)
    {
        HC595_ShiftByte(io_buf[i]);
    }

    HC595_Latch();
}
```

每次 `ExtIO_Set` 都会立刻 `ExtIO_Refresh()`（改一路就整链重发）。

### 6.3 上电

```50:53:User/board_init.c
	/* HC595 / 扩展 IO：启动流程跑到此处后再延时 3s，再上电初始化 */
	delay_ms(3000);
	HC595_Init();
	ExtIO_Init();
```

`ExtIO_Init`：`io_buf[]` 填 `0xFF` → `ExtIO_Refresh` → `HC595_OutputEnable(0)`（输出仍关）。  
之后业务在需要时再 `OE_ENABLE()`（见下）。

---

## 7. 和梯控业务的关系

`floor_ctrl.h`：

```c
#define FLOOR_IO_START 1
#define FLOOR_IO_COUNT 40
```

| 业务意图（典型） | 调用 | 595 / ExtIO 侧 |
|------------------|------|----------------|
| 授权某层 | `Floor_AuthCheck` → `ExtIO_Set(floor, LOW)` | 对应脚拉低，并开 TIM5 超时 |
| 呼一层（脉冲） | `Floor_CallOne` | `OE_ENABLE` → 拉低 → delay → 拉高 → `OE_DISABLE` |
| 常开/限位 | `Floor_AuthCheck_open` / `_limit` | 多路 `ExtIO_Set` / `SetMulti` |
| 全关 / 全开 | `Floor_AllOff` / `Floor_AllOn` | `ExtIO_SetMulti(1, 40, …)` |

楼层号 **1～40** 直接对应扩展 IO **1～40**。权限位图 5 字节 × 8 bit = 40 层，与 `FloorCtrl_*Tail5*` / EEPROM 限层一致。

`Floor_CallOne` 中显式开关 OE 的片段：

```84:94:Hardware/Board/floor_ctrl.c
	if (floor_dbg > 0 && floor_dbg < 41)
	{
		OE_ENABLE();

		ExtIO_Set(floor_dbg, EXT_IO_LOW);

		delay_ms(200);

		ExtIO_Set(floor_dbg, EXT_IO_HIGH);

		OE_DISABLE();
```

部分路径（如 `Floor_DirectGo` / `Floor_AuthCheck`）里 `OE_ENABLE` 仍注释——联调「有位图变化但板子无输出」时先查 **OE 是否打开**。

---

## 8. 上电与数据流

```text
board_init
  delay 3s
  HC595_Init()          // 线初始化，OE 关
  ExtIO_Init()          // 镜像 0xFF，刷新，OE 仍关
主业务
  Floor_* / ExtIO_Set
    → 改 io_buf → ShiftByte×8（U8→U1）→ Latch
    → 需要时 OE_ENABLE 让 Q 端真正驱动外设
```

---

## 9. 联调注意

1. **改脚**：只改 `bsp_hc595.h` 里 PORT/PIN，与原理图一致。  
2. **楼层错乱**：检查级联方向与 `ExtIO_Refresh` 是否仍从 `i=7` 递减。  
3. **无输出**：查 OE（PD4 低才使能）、供电、以及 `ExtIO_Init` 后是否业务侧开过 OE。  
4. **频繁 `ExtIO_Set`**：每次全链刷新；批量可用 `ExtIO_SetMulti`（内部仍多次 Set+Refresh，热路径可再优化为改完再 Refresh 一次）。  
5. **极性**：授权多路是 `EXT_IO_LOW`；硬件若是高有效，需对原理图确认。

---

## 10. 源码索引

| 想搞清楚… | 打开 |
|-----------|------|
| 引脚与移位时序 | `Hardware/Board/bsp_hc595.*` |
| 1～40 IO 镜像 / 反向刷新 | `Hardware/Board/ext_io.*` |
| 楼层呼梯 / 授权 | `Hardware/Board/floor_ctrl.*` |
| 上电顺序 | `User/board_init.c` |
| 权限里写楼层 | `App/Pass/pass_permission.c` → `FloorCtrl_*` / `Floor_*` |
