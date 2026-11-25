# WDG 重构（阶段 3）

> 来源（双层）：驱动取自 **`qibiji_zxl/HARDWARE/WDG/wdg.c/.h`**（应用工程的重写版，2026-11-25 跨模块
> 审计定源），boot 版 `qibiji_boot/HARDWARE/WDG/wdg.c/.h` 仅作对照——两者窗口参数完全一致
> （boot main.c:131 `IWDG_Init(4, 1990); // 3180MS`，zxl 常量化后同一组数），但 zxl 版多了
> 复位来源记录与参数化，且命名不与固件库撞车。
> 本阶段是叶子模块（无依赖、可随时插队落盘），但 **LCD/4G/EEPROM 三个已入库文档都在等 wdg.h**
> （§7），建议尽早落盘。只生成 md，不动工程文件。

## 1. 产出清单与差异

| 文件 | 动作 | 说明 |
|---|---|---|
| `HARDWARE/WDG/wdg.h` | 换源重写 | 窗口参数宏（WDG_PRESCALER/WDG_RELOAD + 三档时长注释）+ `g_wdg_reset_cause` extern + 声明（见 §2） |
| `HARDWARE/WDG/wdg.c` | 换源重写 | 复位来源记录 + 参数化初始化 + 喂狗（见 §3） |
| `USER/main.c` | 替换 | 本阶段验证代码：开狗 + 心跳喂狗 + 复位来源打印（见 §4） |
| Keil 工程 | 手动加 | HARDWARE 组加 `wdg.c`；Include Paths 加 `..\HARDWARE\WDG`（见 §5） |

### 1.1 与原工程的差异（换源 + 命名 + 两处新增，窗口参数不变）

| 差异 | 原工程位置 | 现位置 | 理由 |
|---|---|---|---|
| **换源**：boot `wdg.c/.h` → zxl `wdg.c/.h` | `qibiji_boot/HARDWARE/WDG` | `wdg.c/.h` | 两个工程的窗口参数相同（4,1990），zxl 版在其上多两件事：复位来源记录、参数化常量——都是 bootloader 用得上的（§7.4）；zxl 版是应用工程的重写版，按「重构逻辑参考 zxl」约定取其源 |
| `IWDG_Init(prer, rlr)` → `WDG_Init(void)` | boot wdg.c:9 | wdg.c | **去固件库前缀撞名**：FWLib 的 IWDG 函数族就叫 `IWDG_Init/IWDG_...`（stm32f10x_iwdg.h），用户函数同名同前缀，调用点 `IWDG_Init(4,1990)` 与 `IWDG_WriteAccessCmd(...)` 混排极易读错；参数化后入参收敛进头文件宏，签名变 void |
| `IWDG_Feed()` → `WDG_Feed()` | boot wdg.c:22 | wdg.c | 同上，且与 LCD/4G/EEPROM 文档已引用的名字一致 |
| 魔数 `IWDG_Init(4, 1990)` → `WDG_PRESCALER 4 / WDG_RELOAD 1990` 宏 | boot USER/main.c:131 | wdg.h | 参数属主归位：窗口是驱动属性不是调用方的记忆题；宏旁附三档 LSI 频率换算（30/40/60kHz → 4245/3184/2123ms），最坏窗口 2123ms 是所有阻塞段的上界依据 |
| **新增 `g_wdg_reset_cause` 复位来源记录** | —（boot 无） | wdg.c | zxl 同款（wdg.c:3-10）：WDG_Init 开头读 `RCC_FLAG_IWDGRST` 存入全局、随即清标志。bootloader 上电决策用得上——看门狗复位（OTA 中途异常重启）与冷上电走不同分支（§7.4）；boot 版复位后无从知晓上次为什么重启 |
| `WWDG_Init / WWDG_Set_Counter / WWDG_NVIC_Init` 三个声明 | boot wdg.h:9-11 | **删除** | 死声明：无实现、全工程零调用（grep 证实）；本工程只用 IWDG |
| 类型 `uint8_t` → `u8`；include `stm32f10x.h` → `sys.h` | zxl 源 | 统一 | 工程类型与头文件惯例（u8 经 sys.h typedef） |
| 注释 | 原/源注释 | 全部规范中文注释 | 现行注释规范 |
| 窗口时序 / 寄存器序列 | — | 逐行等价 | 写保护→预分频→重装值→重装载→使能，两版完全一致 |

## 2. wdg.h（换源重写，全文）

```c
#ifndef __WDG_H
#define __WDG_H

#include "sys.h"        /* u8、stm32f10x.h（IWDG/RCC 寄存器与固件库声明经此引入） */

/*
 * 窗口配置：IWDG 时钟源为内部 LSI（数据手册 30~60kHz，典型 40kHz）
 *   分频因子 = 4 * 2^WDG_PRESCALER = 4 * 2^4 = 64
 *   Tout = 64 * WDG_RELOAD / f_LSI
 *     f_LSI = 60kHz → 2123ms（最短，所有单段阻塞必须小于该值）
 *     f_LSI = 40kHz → 3184ms（标称，与原工程 main.c 的 3180ms 一致）
 *     f_LSI = 30kHz → 4245ms（最长）
 * 落盘时改这两个宏即可调窗口；改大后必须同步复核全工程最长阻塞段
 * （LCD 初始化 1.5s、EEPROM 连续写、4G 复位脉冲 0.5s/1s）是否仍在窗口内
 */
#define WDG_PRESCALER        4u
#define WDG_RELOAD           1990u
#define WDG_WINDOW_MIN_MS    2123u

/* 上次复位是否由 IWDG 引起（1=是，0=上电/其它复位）。
 * WDG_Init 时采样（读标志后立即清除），main 在串口初始化完成后打印 */
extern volatile u8 g_wdg_reset_cause;

/* 使能 IWDG（写保护解锁 → 预分频 → 重装值 → 重装载 → 使能）。
 * 注意：一旦使能，软件无法关闭，只能靠复位恢复 */
void WDG_Init(void);

/* 重装载 IWDG 计数器（喂狗）。喂狗间隔必须小于最坏窗口 WDG_WINDOW_MIN_MS */
void WDG_Feed(void);

#endif
```

## 3. wdg.c（换源重写，全文）

```c
/* ===========================================================================
 * 独立看门狗（IWDG）驱动层 - qibiji_bootloader
 * 换源说明：取自 qibiji_zxl 应用工程的重写版（窗口参数与原 boot 版一致：
 * 64 分频 × 1990 重装 ≈ 3.18s@40kHz），仅做 u8 化与注释规范。
 * 相对 boot 版的两处增量：① 复位来源记录（看门狗复位 vs 冷上电，上电决策用）；
 * ② 窗口参数常量化（不再靠调用方记 prer/rlr 魔数）。
 * =========================================================================== */
#include "wdg.h"

volatile u8 g_wdg_reset_cause = 0u;

/* 初始化独立看门狗：窗口 3.18s@40kHz LSI，最坏情况 2.12s */
void WDG_Init(void)
{
    /* 1) 先记录复位来源，再清标志；打印交给 main（此时串口可能还没初始化） */
    g_wdg_reset_cause = (RCC_GetFlagStatus(RCC_FLAG_IWDGRST) != RESET) ? 1u : 0u;
    RCC_ClearFlag();

    /* 2) 写保护 → 预分频 → 重装值 → 重装载 → 使能 */
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(WDG_PRESCALER);
    IWDG_SetReload(WDG_RELOAD);
    IWDG_ReloadCounter();
    IWDG_Enable();
}

/* 喂独立看门狗 */
void WDG_Feed(void)
{
    IWDG_ReloadCounter();
}
```

## 4. 验证 main（替换 LCD 验证 main 或按落盘顺序插队）

验证目标三个：**① 开狗后按节拍喂狗正常运行**；**② 停止喂狗后在窗口内复位**；
**③ 复位来源记录链路通**（复位后 `g_wdg_reset_cause` 报告 1）。
本验证 main 是独立的（不依赖 LCD/4G/EEPROM），随时可单独落盘运行。

```c
/* USER/main.c —— WDG 阶段验证：开狗 → 心跳喂狗 → （手动）停喂验证复位与来源记录 */
#include "stm32f10x.h"
#include "sys.h"
#include "delay.h"
#include "usart.h"
#include "led.h"
#include "wdg.h"

static u32 blink_cnt = 0;   /* 心跳计数 */

int main(void)
{
    delay_init();               /* SysTick 时基 */
    NVIC_Configuration();       /* 中断分组 */
    uart1_init(115200);         /* 调试口 */
    LED_Init();
    LED_CLOSE();

    /* 复位来源在 WDG_Init 里采样（此刻串口刚初始化完，可以直接打印）。
     * 第一次上电应是 power-on；停喂复位重跑后应显示 iwdg */
    WDG_Init();
    printf("wdg test start, window=%ums, reset cause: %s\r\n",
           (unsigned int)WDG_WINDOW_MIN_MS, (g_wdg_reset_cause != 0u) ? "iwdg" : "power-on");

    while (1)
    {
        delay_ms(500);              /* 半秒节拍：喂狗间隔 0.5s << 最坏窗口 2.12s */
        WDG_Feed();                 /* 【验证复位时注释本行】停喂后约 3.2s（40kHz 标称）复位 */
        LED_ERR = !LED_ERR;         /* 灯光心跳 */
        blink_cnt++;
        printf("wdg alive, cnt=%u\r\n", (unsigned int)blink_cnt);
    }
}
```

**测试方法（两步）**：

1. **正常喂狗**：上电看 `wdg alive` 持续递增、无复位——窗口内喂狗运行正常；
2. **停喂复位 + 来源记录**：临时注释循环里的 `WDG_Feed()` 重新烧录——上电约 3.2s 后心跳停止、
   板子自动复位、串口重新打印 `wdg test start ... reset cause: iwdg`——窗口与复位来源记录
   两条链路同时闭环。验证完恢复喂狗行。

**注意**：开狗后所有阶段验证 main 都受窗口约束——LCD 四幕演示（单轮 >3s）超窗，
**LCD 验证 main 不开狗**（其文档 §7 已注明）；本阶段验证 main 是独立代码，不冲突。

## 5. Keil 配置要点

| 配置项 | 值 | 说明 |
|---|---|---|
| 分组 | HARDWARE 组加 `wdg.c` | `wdg.h` 同目录自动可寻 |
| Include Paths | 加 `..\HARDWARE\WDG` | main.c include "wdg.h" 需要 |
| FWLib 勾选 | 无新增 | `IWDG_*` 在 `stm32f10x_iwdg.c`、`RCC_GetFlagStatus` 在 `stm32f10x_rcc.c`，均已勾 |
| stm32f10x_conf.h | 无新增 | 已确认包含 `stm32f10x_iwdg.h`（bootloader conf.h:41） |

## 6. 验证清单（全部通过才算本阶段完成）

1. 编译 0 Error；
2. 串口 `wdg test start` + `reset cause: power-on`（首次上电），随后 `wdg alive` 0.5 秒一条持续递增；
3. 注释喂狗行复测：约 3.2s 后自动复位，重启后 `reset cause: iwdg`——窗口时长与来源记录双通过；
4. 恢复喂狗行复测：持续运行不复位（回归确认）；
5. map 文件确认：`wdg.o` 已链接，`WDG_Init / WDG_Feed / g_wdg_reset_cause` 符号存在，`IWDG_Init/IWDG_Feed` 用户符号不存在（旧名已废）。

## 7. 跨阶段边界（WDG）

1. **LCD（已入库，依赖闭环）**：`LCD12864_Init` 的 1.5s 上电稳定期分三片、每片后 `WDG_Feed()`
   ——wdg.h 入库后 LCD 文档 §10.5 的「临时注释」策略作废，三处喂狗自然生效；
2. **4G（已入库）**：`Gprs4G_Init` 条件复位脉冲末尾 `WDG_Feed()`（500ms 延时占窗口 1/4）；
3. **EEPROM（已入库）**：`AT24CXX_WriteOneByte` 每字节写周期后 `WDG_Feed()`（连续写累计秒级）；
4. **正式 main（阶段 12）**：三件事——① 上电打印 `g_wdg_reset_cause`（看门狗复位 = OTA 中途
   异常重启，可与 UPDATE_flag 联合决策重试或放弃，具体策略阶段 12 定）；② 主循环节拍处喂狗
   （原 boot main.c:131/136 的 `IWDG_Init(4,1990)` / `IWDG_Feed()` 调用点由 `WDG_Init()/WDG_Feed()`
   等价替换，魔数进宏）；③ 全工程阻塞段审查：任何单段延时 ≥ `WDG_WINDOW_MIN_MS` 都必须
   分片喂狗（LCD 的 3×500ms 分片就是样板）；
5. **BOOT（阶段 11）**：IWDG 一旦使能软件关不掉、跳 A1 后继续计时——`BootLoader_Clear()`/
   `JumpToApp()` 跳转前 `WDG_Feed()` 一次，把整个窗口（≥2.1s）留给 A1 应用启动到第一次喂狗；
   A1 应用工程自带 WDG（zxl 同款）会接续喂狗。登记 BOOT 文档阶段 11 落实；
6. **依赖闭环**：无依赖（叶子模块），迁移顺序表「WDG/LED/CRC16/cJSON 无依赖」成立；
   落盘顺序可插队——LCD/4G/EEPROM 三个文档的 wdg.h 引用在落盘本阶段后全部自然生效。

---

本文档只生成/修订 md，不改任何 `.c/.h` 工程文件；`wdg.h / wdg.c / main.c` 由你按本文档落盘。
