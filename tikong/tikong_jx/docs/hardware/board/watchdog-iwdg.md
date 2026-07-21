# 独立看门狗 IWDG（本工程）

更新时间：2026-07-19

> **相关**：[app-poll-flow.md](./flows/app-poll-flow.md)、[onboarding-f103.md](./onboarding-f103.md)、[embedded-coding-style.md](./embedded-coding-style.md) §6.3 / §8  
> **源码**：`Hardware/Board/wdg.c`、`wdg.h`；调用在 `User/main.c`

---

## 1. 作用

看门狗在超时内若未被「喂狗」，会强制 MCU **复位**，用来从主循环卡死、死等、异常死循环中恢复。

本工程启用的是 **IWDG（Independent Watchdog，独立看门狗）**：时钟来自片内 **LSI**，与主时钟相对独立；主频异常时仍可能复位。

---

## 2. 本工程用哪种

| 类型 | 本工程 |
|------|--------|
| **IWDG** | **在用**：`IWDG_Init` / `IWDG_Feed` |
| **WWDG**（窗口看门狗） | `wdg.h` 仅有声明，**无实现、无调用** |

排障与改参时只看 IWDG 即可。

---

## 3. 超时怎么算

`wdg.c` 注释公式（LSI 按约 **40 kHz** 估算）：

```text
Tout(ms) ≈ ((4 * 2^prer) * rlr) / 40
```

- `prer`：预分频档位（0～7，低 3 位有效）；分频因子 = `4 * 2^prer`（最大按 256 理解）  
- `rlr`：重装载值（低位有效）

**现参**（`main.c`）：

```c
IWDG_Init(4, 1990); // 注释：3180MS
```

代入：`prer=4` → 分频因子 `4*2^4 = 64`  

`Tout ≈ (64 * 1990) / 40 ≈ 3184 ms ≈ 3.18 s`

即：若约 **3.2 秒**内没有任何一次有效喂狗，IWDG 触发复位。

> LSI 有偏差，实际超时会略漂；以注释「约 3180 ms」作工程量级即可。

---

## 4. 谁在什么时候喂狗

```text
board_init() → app_init() → printf("sys init ok")
       ↓
IWDG_Init(4, 1990)   // 打开 IWDG
IWDG_Feed()          // 立刻装载计数
       ↓
while (1) {
    IWDG_Feed();     // ★ 每一圈循环开头喂狗
    if (AT 序列请求)
        uart3_at_sequence_poll();   // 可能多圈才完成
    else
        app_poll();                 // PC / 读头 / 4G 业务
    // … 限层到期等其它逻辑
}
```

要点：

- **喂狗只在 `main` 循环入口**（以及 init 后那一次），**不在**各 `*Process` / ISR 里喂。  
- 某一路业务（长 JSON、EEPROM 连写、AT 等待）若**单次占用超过约 3.2 s 且中间回不到 `while` 开头**，会复位。  
- AT 配网与 `app_poll` 互斥，但两者都仍处在同一 `while` 里，**每圈仍会 Feed**；卡死的是「进不了下一圈」，不是「走了 AT 就不喂狗」。

---

## 5. 驱动在做什么

`Hardware/Board/wdg.c`：

| 函数 | 行为 |
|------|------|
| `IWDG_Init(prer, rlr)` | 开写保护 → 设预分频/重装载 → `ReloadCounter` → `Enable` |
| `IWDG_Feed()` | `IWDG_ReloadCounter()`，把计数重装回 `rlr` |

底层为 StdPeriph：`IWDG_WriteAccessCmd` / `IWDG_SetPrescaler` / `IWDG_SetReload` 等。

`IWDG_Feed` 里曾有按 `flag100ms` 节流喂狗的注释代码，**现已旁路**，等于每调一次就 reload。

---

## 6. 调试与联调注意

1. **断点停太久**（超过约 3 s）→ 很容易 IWDG 复位，表现为「一停就重启」。  
2. 需要长时间单步时：临时拉长超时（增大 `rlr` 或调整 `prer`）、或调试期注释 `IWDG_Init`（**改完务必恢复**）。  
3. 部分调试器可配置「调试时冻结 IWDG」（`DBGMCU` 相关）；本工程未在业务里统一封装，以你本机调试选项为准。  
4. 若现场「无故复位」，除业务死循环外，也要怀疑：**主循环被某次同步操作堵住超过超时**。

---

## 7. 源码索引

| 文件 | 内容 |
|------|------|
| `Hardware/Board/wdg.h` | `IWDG_Init` / `IWDG_Feed`；未用的 `WWDG_*` 声明 |
| `Hardware/Board/wdg.c` | IWDG 实现与超时公式注释 |
| `User/main.c` | `IWDG_Init(4, 1990)`；`while` 首行 `IWDG_Feed()` |

---

## 8. 和文档里其它说法的关系

- [app-poll-flow.md](./flows/app-poll-flow.md)：业务轮询图里已标「每圈开头喂狗」。  
- [onboarding-f103.md](./onboarding-f103.md) 常见注意点：断点过久可能复位 —— 原因即本文超时。  
- 通用习惯：[embedded-coding-style.md](./embedded-coding-style.md)（阻塞与看门狗）。
