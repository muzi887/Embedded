# Eeprom_Wpara() 代码解读

> **源码位置**：`qibiji/HARDWARE/REG/reg.c` L203–L225（函数本体，共 23 行）
> **配套文件**：`reg.c`（L15–16 全局量、L25–98 上电读回、L101–138 参数还原、L227–243 首次播种）、`reg.h`、`24cxx.h`、`24cxx.c`、`USER/main.c`、`HARDWARE/SWITCH/switch.c`、`HARDWARE/DC_MOTOR/dc_motor.c`、`HARDWARE/Lcd12864/Dispaly.c`、`HARDWARE/TIMER/timer.c`、`HARDWARE/GPRS/gprs_JSON.c`
> **文档性质**：代码解读笔记，**未修改任何源码**。行号基于 2026-11-11 的工作副本。
> **姊妹文档**：`docs/Dcmotor_Process_explained.md`（自动校准状态机）、`docs/Lcd_Process_explained.md`（设置页 `setpage()` 的直接落盘）、`docs/Gprs_Process_explained.md`（云端 `config.push` 落盘）

---

## 0. 一句话总览

`Eeprom_Wpara()` 是**运行中闸门参数的"延迟落盘信箱"**：中断里发现「圈数变了 / 校准完了」时不能自己写 EEPROM，只置一个标志 + 一个命令码；主循环每轮来查，有信就按命令码把对应参数写进 AT24C02，然后清标志。

它本身不做判断、不产生数据、不校验结果，只是一次**单槽、一次性消费、无返回**的分发：

```c
if (Witeflag == 0) return;   // 没信就走
Witeflag = 0;                // 取走即清
switch (EepromAddr) { ... }  // 按命令码写两个字节
```

整套设计要解决的是一个很具体的矛盾：**编码器脉冲每 5ms 都可能改一次参数，但 EEPROM 每写 1 字节要死等 10ms，而且有擦写寿命**。所以不能在中断里写，也不能每轮都写，必须「攒到一个稳定时刻、由主循环代写一次」。

---

## 1. 它在系统里干什么（业务定位）

### 1.1 一句话 + 一个比喻

闸门运行时，`Num_count`（螺纹圈数）由编码器脉冲实时增减，由此算出开度。这些值断电后必须还在，否则下次上电不知道闸门停在哪、不知道全行程是多少圈。

把 EEPROM 想成一块**需要 10ms 才能刻完一个字、且刻多了会磨损的石板**；中断服务函数是**站在传送带旁的记账员**，它不能停下来刻石板（会丢下一个脉冲），只能往一个只有一格的信箱里塞张纸条：「第 6 号数变了，请刻」。`Eeprom_Wpara()` 是**每轮巡逻的刻字工**：看到纸条就刻一格，刻完把纸条拿走。

### 1.2 为什么必须"延迟"，而不是中断里直接写

| 约束 | 后果 |
|---|---|
| `AT24CXX_WriteOneByte()` 每字节固定 `delay_ms(10)`（`24cxx.c:65`） | 一次 `WriteWord` = 2 字节 ≈ **20ms 纯死等**；TIM2 每 5ms 一次中断，直接在中断里写会压垮节拍、丢脉冲 |
| I2C 位时序靠软件 `delay_us()` 翻转 GPIO（`myiic.c:89-108`） | 长时间关中断或被打断会破坏时序，EEPROM 直接 NACK |
| AT24C02 标称约 100 万次擦写 | 编码器每个脉冲都写一次的话，几次开合就把寿命耗光 |
| 主循环要喂 3180ms 看门狗（`main.c:29,60`） | 长停摆必须被切成片段，靠 `WriteOneByte` 里的 `IWDG_Feed()` 兜底（`24cxx.c:66`） |

所以链路被刻意拆成「**中断只置标志 → 主循环集中写**」，这与 `PropertyReport_SetPending()` 的写法是同一个套路：`ChangeReality()`（`switch.c:130`）也不在中断里组 JSON，只置一个 pending，由主循环 `PropertyReport_Process()` 消费。区别是**属性上报有 1s 节流可以比较频繁，EEPROM 落盘要等"停机且数值稳定"才允许一次**。

### 1.3 它管哪三个参数

| 参数 | 含义 | 谁改它 |
|---|---|---|
| `armReg.gate_maxturns` | 闸门全行程对应的螺纹圈数（标定基准） | `JudeDir()` 自动校准抵达上限位时 `= Num_count`（`switch.c:199`） |
| `armReg.gate_kaidu` | 开度百分比 | `ChangeReality()` 每脉冲重算 `Num_count*100/gate_maxturns`（`switch.c:127`） |
| `armReg.gate_kaidu_count` | 当前圈数（断电恢复的位置） | `JudeKaidu()` 停机稳定后 `= Num_count`（`switch.c:159`） |

---

## 2. 调用上下文

### 2.1 主循环位置（`USER/main.c` L58–L84）

```c
while (1)
{
    IWDG_Feed();                  // 喂狗（IWDG_Init(4,1990) ≈ 3180ms）
    sbf20_poll();                 // 蓝牙
    if (uart3_at_sequence_main_loop_tick()) continue;  // 近场 AT 配置期间独占主循环
    KEY_Process();                // 按键
    AD_Process();                 // 电流采集 / 堵转判定
    Dcmotor_Process();            // 电机状态机 + 自动校准
    Lcd_Process();                // 显示
    PropertyReport_Process();     // 消费中断置位的属性上报
    Gprs_Process();               // 4G 收发队列
    Eeprom_Wpara();               // ← 本文主角：写 EEPROM
    manage485();                  // RS485
}
```

要点：

- **无节拍门控**，每个主循环都跑一遍；真正的频率由 `Witeflag` 决定，绝大多数轮次第一行就 `return`。
- **排在 `Gprs_Process()` 之后、`manage485()` 之前**。一次落盘约 20ms 的阻塞放在 4G 之后，不会延迟本轮的收发队列；但会推迟本轮 RS485 处理。
- **AT 配置期间会被跳过**：`uart3_at_sequence_main_loop_tick()` 返回真时 `continue`，此时 `Witeflag` 会一直挂着，等 AT 序列退出后才补写。这是「可能积压」的一个来源。
- **初始化侧**：`main.c:49-51` 的 `AT24CXX_Init()` → `para_read()`（含 `AT24CXX_Check()` 与 `eepromToPara()`）→ `initReg()` 先完成读回，之后主循环才可能写。

### 2.2 节拍来源（`HARDWARE/TIMER/timer.c` L213–L264）

TIM2 是 5ms 中断（`TIM2_Int_Init(7199, 50)`，`main.c:54`）：

| 频率 | 中断里做什么 | 与本函数的关系 |
|---|---|---|
| 每 5ms | `sys_ms += 5`、`JudeCount()`（编码器）、`JudeDir()`（限位/行程） | `JudeDir()` 是**触发链 A** 的源头 |
| 每 10ms | `JudeLocked()`（堵转） | 无关 |
| 每 20ms | `flag20ms = 1` | 无关（ADC 用） |
| 每 300ms | `flag300ms = 0x03` | 驱动 `dcmotor_auto()` 的校准状态机 |
| **每 500ms** | `flag500ms = 1`、`JudeKaidu()` | `JudeKaidu()` 是**触发链 B** 的源头 |
| 每 1s / 1min | `flag1s` / `flag1min` | 无关 |

**关键**：两个置位者都在 TIM2 中断上下文，而消费者 `Eeprom_Wpara()` 在主循环。这是一组典型的**中断生产者 / 主循环消费者**，后面 §10 会讲它没有加 `volatile` 的问题。

---

## 3. 两个全局状态量

### 3.1 定义（`reg.c` L15–16，`reg.h` L16–17）

```c
u16 EepromAddr = 0;
u8  Witeflag   = 0;   // 写入地址和写入标志
```

| 变量 | 类型 | 写者 | 读者 | 语义 |
|---|---|---|---|---|
| `Witeflag` | `u8` | `switch.c:168`、`switch.c:201`（中断）置 1；`reg.c:207`（主循环）清 0 | `reg.c:205` | 「信箱里有纸条」 |
| `EepromAddr` | `u16` | `switch.c:169`（= `0x0006`）、`switch.c:202`（= `0x0004`）（中断） | `reg.c:208` | 「要刻哪一个参数」的**命令码** |

它们构成一个**深度为 1 的队列**：没有 `head/tail`、没有计数、没有环形缓冲，只有「一个标志 + 一个编号」。

### 3.2 最容易踩的坑：`EepromAddr` 不是 EEPROM 地址

变量名叫 `Addr`，值又写成 `0x0004` / `0x0006` 这种十六进制，看上去就是字节地址——**但完全不是**。看 switch 的映射：

| `EepromAddr`（命令码） | 实际写入的字节地址 | 写入的参数 | 数值规律 |
|---|---|---|---|
| `0x0004` | `0x0008` | `armReg.gate_maxturns` | `4 × 2 = 8` |
| `0x0005` | `0x000A` | `armReg.gate_kaidu` | `5 × 2 = 10` |
| `0x0006` | `0x000C` | `armReg.gate_kaidu_count` | `6 × 2 = 12` |

而 `reg.h:79-84` 恰好写着：

```c
//下列几个宏是缓冲数组下标（字序号），不是 EEPROM 的字节地址；字节地址 = 下标 * 2
#define GATE_MAXTURNS_WORD_IDX      4   //闸门高差对应的螺纹圈数计数值（字节 0x0008）
#define GATE_KAIDU_WORD_IDX         5   //闸门实际开度值（字节 0x000A）
#define GATE_KAIDU_COUNT_WORD_IDX   6   //闸门实际开度对应的螺纹圈数计数值（字节 0x000C）
```

**数值完全对上**：`EepromAddr` 的 4/5/6 与 `eeprom_buf[]` 的**字索引**一致，目标字节地址 = 索引 × 2。所以这个 switch 本质上等价于 `AT24CXX_WriteWord(EepromAddr * 2, ...)`,只是把三个 value 用硬编码展开了（源码没有这样实现，这里只是指出数值规律，**属于静态推断，源码未明示设计意图**）。

更坑的是数值撞车：

| `EepromAddr` 的值 | 若误当成字节地址，会以为是 | 实际写到 |
|---|---|---|
| `0x0004` | 堵转电流阈值 `motor_warning_curr_upper` | `0x0008` 圈数 |
| `0x0006` | 闸门最大高度 `gate_maxheight` | `0x000C` 圈数计数 |

这三个参数区地址是 `paraToEeprom()` / `setpage()` / `config.push` 在用的，与 `Eeprom_Wpara()` **恰好错开**，所以目前不会互相踩；但阅读代码时极易误判成「`Eeprom_Wpara()` 在改电流阈值」。这一点在 `docs/Lcd_Process_explained.md` §11 里被称作「同名异物」。

---

## 4. 函数逐行解读（`reg.c` L203–L225）

```c
//eeprom
void Eeprom_Wpara()
{
    if (Witeflag == 0)              // L205  空信箱，直接返回（绝大多数轮次走这里）
        return;
    Witeflag = 0;                   // L207  取走即清：一次性消费，不重试、不排队
    switch (EepromAddr)             // L208  按命令码分发
    {
    case 0x0004:                                                    // L210
        AT24CXX_WriteWord(0x0008, armReg.gate_maxturns);             // L211  写全行程圈数
        printf("w_gate_rpm=%d\n", armReg.gate_maxturns);             // L212
        break;
    case 0x0005:                                                    // L214
        AT24CXX_WriteWord(0x000A, armReg.gate_kaidu);                 // L215  写开度
        //printf("w_gate_kaidu=%d\n", armReg.gate_kaidu);             // L216  日志被注释
        break;
    case 0x0006:                                                    // L218
        AT24CXX_WriteWord(0x000C, armReg.gate_kaidu_count);           // L219  写当前圈数
        printf("w_gate_kaidu_count=%d\n", armReg.gate_kaidu_count);   // L220
        break;
    default:                                                        // L222
        break;                                                      // L223  静默丢弃
    }
}
```

逐条说明：

- **L205 的门**：没有 `else`、没有超时、没有超时后的兜底写。只要没标志就什么也不做。
- **L207 的清零时机在 switch 之前**：即使命令码是无效值（`default`），标志也已经清了，这张纸条被**静默丢掉**，不会留到下一轮重试。这是「一次性消费」语义，不是「处理成功才清」。
- **L211 / L215 / L219 都调 `AT24CXX_WriteWord()`**：底层把一个 `u16` 拆成小端两字节、发两笔完整 I2C 事务、每笔后 `delay_ms(10)`，合计约 **20ms**（详见 §9）。
- **函数没有返回值、没有写后读回校验、没有 ACK 检查**。对比云端配置用的 `Config_WriteWordWithVerify()`（`gprs_JSON.c:101-114`）会「读旧值 → 写 → 读回比对 → 不等就回滚」，本函数写失败是**完全静默**的。
- **两个 `printf`**：115200bps 下每条约 1ms 量级，会在落盘路径上再叠一点阻塞；`case 0x0005` 的那条已被注释掉。

---

## 5. 两条触发链

全项目对 `Witeflag` 置 1 的地方**只有两处**，都在 `HARDWARE/SWITCH/switch.c`，都在 TIM2 中断上下文。

### 5.1 链路 A：自动校准得到全行程圈数 → `case 0x0004` → 写 `0x0008`

```
LCD 设置页第 2 项打开"自动校准"
  Dispaly.c:562  autoFlag = 1
        │
        ▼
dcmotor_auto() 每 300ms 步进（dc_motor.c:125-195）
  step0 下降 → step1 到下限位停 3s → step2 上升 → step3 到上限位停 3s → step4 下降 → step5 关校准
        │
        ▼  上升途中编码器每个脉冲
JudeCount() / ChangeReality()（switch.c:55-135）
  Num_count++（autoFlag 时不设上限，L116）
        │
        ▼  闸门触到上限位传感器
JudeDir()（switch.c:189-204）每 5ms
  if (!UP_LIMIT && !FLAG_OPENALL) {
      FLAG_OPENALL = 1;
      if (autoFlag) {
          armReg.gate_maxturns = Num_count;   // L199 标定的基准圈数
          Witeflag   = 1;                     // L201
          EepromAddr = 0x0004;                // L202 命令码：写圈数
      }
  }
        │
        ▼  下一轮主循环
Eeprom_Wpara() → AT24CXX_WriteWord(0x0008, armReg.gate_maxturns)
```

**业务含义**：这是**闸门行程标定**的落盘时刻。只有在 `autoFlag == 1`（人工进入校准模式）时才记；正常开关闸门碰到上限位，`JudeDir()` 只会 `Num_count = armReg.gate_maxturns`（L185）做位置校正，**不会**触发写。

### 5.2 链路 B：停机后圈数稳定 → `case 0x0006` → 写 `0x000C`

```
任意一次闸门运动
  ChangeReality() 每个编码器脉冲：Num_count ±1、重算 gate_kaidu、changeFlag = 1（L133）
        │
        ▼  电机停下（sys_moter_state == MOTOR_STOP）
JudeKaidu()（switch.c:138-173）每 500ms 由 TIM2 调用
  if (motor_on_off == MOTOR_STOP && changeFlag) {
      if (LastNumCount != Num_count)  LastNumCount = Num_count;  // 还在变，继续等
      else {                                                      // 连续两次采样一致
          changeFlag = 0;
          armReg.gate_kaidu_count = Num_count;
          Witeflag   = 1;
          EepromAddr = 0x0006;
      }
  }
        │
        ▼  下一轮主循环
Eeprom_Wpara() → AT24CXX_WriteWord(0x000C, armReg.gate_kaidu_count)
```

**这段"双稳态确认"是整条链最值得注意的设计**：`LastNumCount` 与 `Num_count` 相等才落盘，等于要求「电机已停 **且** 连续两个 500ms 采样周期圈数没变」（至少 500ms、最长 1s 的稳定窗口）。目的有三：

1. 避免在运动过程中每 5ms 写一次 EEPROM（寿命 + 阻塞）；
2. 避开停机瞬间的机械惯性/编码器抖动，写进去的是**真正静止的位置**；
3. 把「一次开合动作」收敛成**一次**落盘，而不是几十次。

**业务含义**：这是**断电位置恢复**的落盘时刻。下次上电 `eepromToPara()` 用 `Num_count = armReg.gate_kaidu_count`（`reg.c:136`）把位置还原，闸门才知道自己停在哪。

### 5.3 两条链的时间尺度对比

| | 链路 A（圈数标定） | 链路 B（位置落盘） |
|---|---|---|
| 触发频率 | 人工校准时，一趟行程一次 | 每次开合动作停机后一次 |
| 节拍 | TIM2 每 5ms 检测限位 | TIM2 每 500ms 检测稳定 |
| 前置条件 | `autoFlag == 1` | `MOTOR_STOP && changeFlag && 数值稳定` |
| 写入地址 | `0x0008` | `0x000C` |
| 上电还原 | `armReg.gate_maxturns = eeprom_buf[4]` | `Num_count = eeprom_buf[6]` |

---

## 6. `case 0x0005` 是死分支

全项目对 `EepromAddr` 的赋值只有 `switch.c:169`（`0x0006`）和 `switch.c:202`（`0x0004`），初值 `0`（`reg.c:15`）。**没有任何代码会把它置成 `0x0005`**，因此 `AT24CXX_WriteWord(0x000A, armReg.gate_kaidu)` 永远不会执行。

### 这会不会造成故障？不会，因为开度是派生量

上电读回 `eepromToPara()`（`reg.c:101-138`）虽然把 `0x000A` 读进 `armReg.gate_kaidu`（L128），但紧接着 L134 又用下面这行**覆盖**：

```c
armReg.gate_kaidu = armReg.gate_kaidu_count * 100 / armReg.gate_maxturns;
```

也就是说开度始终由「当前圈数 ÷ 全行程圈数」现算，EEPROM 里 `0x000A` 存的是什么**根本不影响运行结果**——`paraToEeprom()` 播种时写的那个值（全局结构体初值 0）会一直躺在 EEPROM 里，谁也不读它做决策。

这反倒说明一件事：**真正必须持久化的只有两个量**——`gate_maxturns`（标尺）和 `gate_kaidu_count`（位置）。开度、百分比、高度都是它们的派生值，不需要存。`case 0x0005` 大概率是早期版本遗留的冗余分支。

> 顺带一个静态风险：`eepromToPara()` L134 的除法没有判 `gate_maxturns == 0`。正常路径下 `paraToEeprom()` 会播种 80、`para_read()` 失败分支也置 80，但如果 EEPROM 恰好被写成 0 而标记字节仍有效，这里会除零。与本函数无关，仅记录。

---

## 7. 落盘地址总表与读写闭环

### 7.1 AT24C02 参数区（`reg.h` L5–13、L79–84）

| 字节地址 | `eeprom_buf[]` 下标 | 参数 | 谁负责写 |
|---|---|---|---|
| `0x0000` / `0x0002` | 0 / 1 | `range_target` / `range_reality` | 已注释停用，仅占坑 |
| `0x0004` | 2 | `motor_warning_curr_upper`（堵转阈值 mA） | `setpage()` / `config.push` / `paraToEeprom()` / `eepromToPara()` 迁移 |
| `0x0006` | 3 | `gate_maxheight`（全开高度 mm） | `setpage()` / `config.push` / `paraToEeprom()` |
| **`0x0008`** | 4 | `gate_maxturns` | **`Eeprom_Wpara()` case 0x0004** / `paraToEeprom()` |
| `0x000A` | 5 | `gate_kaidu` | 仅 `paraToEeprom()` 播种；`case 0x0005` 死分支 |
| **`0x000C`** | 6 | `gate_kaidu_count` | **`Eeprom_Wpara()` case 0x0006** / `paraToEeprom()` |
| `0x000E`–`0x0013` | 7–9 | 保留 | 无 |
| 64 / 96 / 128 / 160 | — | MQTT PK / DK / Secret / Addr，各 32 字节 | `sbf20_app.c` 蓝牙配置、`mqtt_eeprom_init_defaults()` |
| 200 | — | `OTA_InfoCB` | `OTAupdateFlagPersist()` |
| 250 | — | `0x55` 存在标记 | `AT24CXX_Check()` |

所有地址都 ≤ 250，正好落在 AT24C02 的 256 字节容量内（`EE_TYPE = AT24C02`，`24cxx.h:16`）。

### 7.2 闭环

```
     ┌────────────────── 上电 ──────────────────┐
     │  para_read() → AT24CXX_Check()            │
     │    → AT24CXX_Readdatas(0x0000,            │
     │         eeprom_buf, 10)  【读 10 个 u16】 │
     │    → eepromToPara() → armReg / Num_count  │
     └───────────────────────────────────────────┘
                        │
                        ▼  运行中
   中断置 Witeflag + EepromAddr → Eeprom_Wpara()
                        │        AT24CXX_WriteWord(0x0008 / 0x000C, ...)
                        ▼
              AT24C02（掉电不丢）
                        │
                        └─────── 下次上电回到顶部
```

读回用的 `AT24CXX_ReadLenByte()`（`24cxx.c:98-108`）与写入的 `AT24CXX_WriteWord()`（`24cxx.c:111-117`）**字节序自洽**：写是小端（低字节在低地址），读时先读高地址当高字节再左移 8 位拼低字节。所以 `eeprom_buf[4]` 拿到的就是当初写进去的 `u16`。

---

## 8. 与其他三条写入入口的分工

项目里共有**四条**往 EEPROM 写参数的路，彼此地址不重叠、触发条件不同，别混为一谈：

| 入口 | 触发时机 | 写入地址 | 是否经过 `Eeprom_Wpara()` | 有无校验 |
|---|---|---|---|---|
| `paraToEeprom()`（`reg.c:227-243`） | 首次上电 / 标记丢失但数据空白 | `0x0004`–`0x000C` 共 5 个 word + MQTT 默认凭据 | 否，直接调 `WriteWord` | 无 |
| `setpage()`（`Dispaly.c:531,550`） | LCD 设置页输完数字按 `KEY_ENTER` | `0x0006`（最大高度）、`0x0004`（堵转阈值） | **否，绕过命令码机制直接写** | 无 |
| `Config_WriteWordWithVerify()`（`gprs_JSON.c:101-114`） | 云端 `config.push` 下发 | `0x0004`、`0x0006` | 否 | **有**：读回比对，失败回滚旧值 |
| **`Eeprom_Wpara()`** | **运行中圈数/位置变化，主循环延迟写** | **`0x0008`、`0x000C`** | — | **无** |

`AT24CXX_Check()`（`24cxx.c:124-163`）还有一层保护：标记字节 `0x55` 丢了但 MQTT 凭据和闸门标定值仍有效时，**只补标记、绝不回写默认值**，避免把真实 `deviceKey` 覆盖成 `dev001`。

---

## 9. 时序与阻塞代价

一次 `Eeprom_Wpara()` 命中时的开销：

```
AT24CXX_WriteWord(addr, value)
  ├─ AT24CXX_WriteOneByte(addr,   低字节)
  │    IIC_Start → 0xA0 → addr → data → IIC_Stop   （软件位时序，约几百 µs）
  │    delay_ms(10)   ← EEPROM 内部烧写 tWR，必须等满，期间对 I2C 一律 NACK
  │    IWDG_Feed()    ← 24cxx.c:66，防止长写越过 3180ms 看门狗
  └─ AT24CXX_WriteOneByte(addr+1, 高字节)
       同样一整套事务 + 10ms
  ≈ 20ms 主循环停摆（再加 printf 约 1ms）
```

影响与兜底：

- **主循环停 20ms**：这一轮 `manage485()` 被推迟，`Gprs_Process()` 已在前面跑过。20ms 相对 4G 队列 300ms 的 JSON 驻留和 1000ms 的 AT 超时不算长。
- **下行帧不会丢**：这期间若 USART2 收到帧，IDLE 中断仍会把它 `memcpy` 进 `rxStage` 双槽暂存（`usart2.c:142-185`），等主循环回来由 `UartRx_PumpFromDma()` 取走。注意 `Eeprom_Wpara()` 走的是 `WriteWord → WriteOneByte`，**不经过** `AT24CXX_Write()`，所以不会触发后者每 8 字节一次的 `UartRx_PumpFromDma(0)`（`24cxx.c:197-198`）——但 20ms 远小于双槽能扛的窗口，不构成问题。
- **两笔事务不原子**：低字节写完、高字节没写时掉电，会读回「半新半旧」的撕写值。本函数没有读回校验也没有回滚，比 `Config_WriteWordWithVerify()` 更脆弱。详见本节 §9.1。

### 9.1 补充：底层两函数 `AT24CXX_WriteWord` / `AT24CXX_WriteOneByte`

本函数所有落盘最终都收敛到这一对（`qibiji/HARDWARE/24CXX/24cxx.c`）。**关系**：`WriteOneByte` 是唯一真正碰 I2C 总线的写函数；`WriteWord` 自己一行时序都不发，只把一个 `u16` 拆成高低两字节、连调两次前者。

**`AT24CXX_WriteOneByte(addr, data)`（L46-67）** 发一笔完整 I2C 单次写事务：`IIC_Start()` → 器件地址 `0xA0 + ((addr/256)<<1)` + 方向位 0 → `IIC_Wait_Ack()` → 8 位字地址 `addr%256` → ACK → 数据 → ACK → `IIC_Stop()`，然后 **`delay_ms(10)`**、**`IWDG_Feed()`**。

- `EE_TYPE = AT24C02`（`24cxx.h:16`）容量正好 256 字节，所以 `addr/256` 恒为 0、器件地址恒 `0xA0`；`if (EE_TYPE > AT24C16)` 那个 16 位地址分支是给大容量芯片留的通用模板，本项目是**死代码**。
- `delay_ms(10)` 是硬性的：`IIC_Stop()` 后 EEPROM 要花最多约 10ms 把数据从页缓冲烧进浮栅（tWR），期间对任何 I2C 命令都 NACK，紧接着写下一字节必须等满，否则静默丢写。这是整个项目 EEPROM 慢的根源（1 字节 = 10ms，32 字节 MQTT 凭据 = 320ms，128 字节 = 1.28s）。
- `IWDG_Feed()` 放在每字节之后：主循环被 10ms × N 的死等卡住时没法喂狗，独立看门狗窗口约 3.18s，长写会熬到复位；就地喂一次把长停摆切成安全片段。`AT24CXX_Write()`（L182-200）还额外每 8 字节调 `UartRx_PumpFromDma(0)` 搬走停摆期间到达的下行帧。

**`AT24CXX_WriteWord(addr, data)`（L111-117）** 只做拆包，变量名有误导性：

```c
u8 temp_l = (u8)(Data >> 8);      // 名叫 l，拿的却是【高】字节
u8 temp_h = (u8)(Data & 0x00ff);  // 名叫 h，拿的却是【低】字节
AT24CXX_WriteOneByte(WriteAddr,     temp_h);  // 低字节 → 低地址
AT24CXX_WriteOneByte(WriteAddr + 1, temp_l);  // 高字节 → 高地址
```

存储结果是**小端序**（低字节在低地址）：`WriteWord(0x0004, 3500)` → 3500 = `0x0DAC`，`[0x0004]=0xAC`、`[0x0005]=0x0D`。读回 `AT24CXX_ReadLenByte(addr,2)`（L98-108）反着拼（先读 `addr+1` 当高字节左移 8 位，再或上 `addr` 的低字节），**写读自洽**。`AT24CXX_WriteLenByte(addr, Data, 2)`（L73-80）与它字节序完全相同、功能等价。

**代价与「撕写」风险**：一次 `WriteWord` = 两笔完整 I2C 事务 + 两次 10ms 死等 ≈ **20ms 以上主循环停摆**，且两笔各自 START/STOP、无原子性——低字节已烧进、高字节未写时掉电，读回就是「新低位 + 旧高位」的乱码。上层 `gprs_JSON.c:101-114` 的 `Config_WriteWordWithVerify()` 因此存在：读旧值 → `WriteWord` → 读回比对 → 不等就回滚。但**底层两个函数都返回 `void`，根本不告诉调用方写没成功**；回滚也只救「本次写失败」，救不了「写到一半掉电」（掉电后程序已不运行）。

**静态问题**：四次 `IIC_Wait_Ack()` 返回值全被丢弃（主工程这版是 `void`，`myiic.c:56` 超时会自己 `IIC_Stop()` 并返回 1，调用方却继续推字节 → 静默写失败）；`qibiji_init` / `qibiji_boot` 两个分支已改成返回 `AT24CXX_OK/ERROR` 并逐步查 ACK，主工程是三套实现里最弱的一版。此外没有页写（24C02 支持 8 字节页写）、没有 ACK polling 代替死等、`addr` 是 `u16` 但芯片只有 256 字节（`addr ≥ 256` 会静默回绕到错误格子）、`temp_l`/`temp_h` 命名反了。

---

## 10. 代码审查发现（仅记录，未修改）

1. **`Witeflag` / `EepromAddr` 缺 `volatile`**。两者都由 TIM2 中断写、主循环读写，声明却是裸 `u8` / `u16`（`reg.c:15-16`）。同项目里 `rxStageFull[]`、`g_uart2_banner_seen` 都加了 `volatile`，这里没有。理论上编译器可以把主循环里的读缓存进寄存器、看不见中断置位；目前因为隔着独立函数调用、未开 LTO 大多能跑，但不严谨。

2. **单槽信箱会丢请求**。`Witeflag` 只有 0/1，`EepromAddr` 只有一格，没有队列。若两次请求在同一主循环周期内先后到达（例如主循环被 `Gprs_Process()` 或蓝牙长写阻塞时），后一个命令码直接覆盖前一个，**先到的那次写永远不会发生**。链路 A 与链路 B 恰好可能在自动校准结束时先后触发。

3. **赋值顺序放大了竞态窗口**。`switch.c:168-169` 和 `:201-202` 都是**先 `Witeflag = 1`、再 `EepromAddr = 0x000X`**。若中断恰好插在两句之间被主循环读取，会用**上一个残留的命令码**写一次，随后 `Witeflag` 被清零，新请求彻底丢失。正确做法是先写命令码、后置标志，或用一个临界区包住两句。

4. **清标志早于写入**（`reg.c:207`）。写失败（ACK 丢失、EEPROM 忙、掉电）时也把标志清了，不重试；`default` 分支更是把无效命令码静默吞掉。

5. **无写后校验**。云端配置有 `Config_WriteWordWithVerify()` 读回比对，本函数没有；底层 `AT24CXX_WriteOneByte()` 在主工程里是 `void` 且**四次 `IIC_Wait_Ack()` 返回值全被丢弃**（`24cxx.c:48-64`），所以写失败对上层完全不可见。`qibiji_init` / `qibiji_boot` 两个分支已改成返回 `AT24CXX_OK/ERROR`，主工程是三套实现里最弱的一版。

6. **函数声明放错头文件**。实现在 `reg.c`，声明却在 `24cxx.h:36`（`void Eeprom_Wpara(void);`），而 `Witeflag` / `EepromAddr` 的 `extern` 在 `reg.h`。`main.c` 同时 include 了两个头才编译通过；按职责应归到 `reg.h`。

7. **命名问题**：`Witeflag` 是 `Write` 的拼写错误；`EepromAddr` 名为地址实为命令码（§3.2）；`Eeprom_Wpara()` 的 `Wpara` 疑似 "Write parameter" 缩写，与 `paraToEeprom()`（写全部默认参数）名字相近但语义完全不同。

8. **中断里的 `printf`**：`switch.c:200` 的 `printf("save maxtruns= %d", Num_count)` 在 TIM2 中断上下文中打印，115200bps 下会占用可观时间，且缺换行符。虽不在本函数内，但属于同一条触发链。

9. **`case 0x0005` 死分支**（§6），以及 `case 0x0004` / `0x0006` 里硬编码的字节地址与 `reg.h` 的 `*_WORD_IDX` 宏没有复用，改地址表时容易漏改。

---

## 11. 三个最容易混的点

| 容易混成 | 实际是 |
|---|---|
| `EepromAddr` 是 EEPROM 字节地址 | 是**命令码 / 字索引**；`0x0004` 实际写到 `0x0008`，`0x0006` 实际写到 `0x000C`。`0x0004` 这个数值本身反而是堵转阈值的地址，纯属撞车 |
| `Eeprom_Wpara()` 负责保存所有设置 | 只管**运行中变化的圈数与位置**（`0x0008`/`0x000C`）。LCD 设置页、云端配置、首次播种都**绕过它**直接写 |
| `Witeflag` 一置位就立刻写 | 要等**下一轮主循环**；且 `JudeKaidu()` 还要求停机后连续两次 500ms 采样数值不变才置位。AT 配置期间会被 `continue` 跳过而继续挂着 |

---

## 12. 一句话串起来

闸门一动，编码器每 5ms 改一次 `Num_count`；中断不敢停下来刻 EEPROM（10ms/字节 + 擦写寿命），只在「自动校准摸到上限位」或「停机后数值连续两次 500ms 不变」这两个稳定时刻，往单槽信箱里塞 `Witeflag=1` 加一个命令码 `0x0004` / `0x0006`；`Eeprom_Wpara()` 每轮主循环来取，取走即清，按命令码把 `gate_maxturns` 或 `gate_kaidu_count` 小端两字节写进 `0x0008` / `0x000C`；下次上电 `para_read()` 再把它们读回 `armReg`，闸门就知道自己有多长行程、停在哪个位置。

---

## 附录：出处对照

| 结论 | 源码位置 |
|---|---|
| 函数本体、switch 三分支 | `qibiji/HARDWARE/REG/reg.c:203-225` |
| `Witeflag` / `EepromAddr` 定义 | `reg.c:15-16`；`extern` 在 `reg.h:16-17` |
| 函数声明（放错头文件） | `qibiji/HARDWARE/24CXX/24cxx.h:36` |
| 主循环调用位置 | `qibiji/USER/main.c:82`；初始化 `main.c:29,49-54` |
| TIM2 节拍与 `JudeKaidu()` 调用 | `qibiji/HARDWARE/TIMER/timer.c:213-264`（`JudeDir` L225、`JudeKaidu` L242） |
| 触发链 A（自动校准落圈数） | `qibiji/HARDWARE/SWITCH/switch.c:176-204`（L197-203） |
| 触发链 B（停机稳定落位置） | `switch.c:138-173`（L159-169）；`ChangeReality()` L94-135 |
| `autoFlag` 置位 / 校准状态机 | `qibiji/HARDWARE/Lcd12864/Dispaly.c:558-570`；`qibiji/HARDWARE/DC_MOTOR/dc_motor.c:125-195` |
| 上电读回与参数还原 | `reg.c:25-98`（`para_read`）、`reg.c:101-138`（`eepromToPara`） |
| 首次播种 / 主循环延迟写入口其他参数 | `reg.c:227-243`（`paraToEeprom`） |
| EEPROM 地址表与字索引宏 | `reg.h:5-13, 79-95` |
| `AT24CXX_WriteWord` / `WriteOneByte` 实现 | `qibiji/HARDWARE/24CXX/24cxx.c:46-67, 111-117` |
| `AT24CXX_Check()` 标记保护 | `24cxx.c:124-163` |
| `AT24CXX_Write()` 长写泵下行帧 | `24cxx.c:182-200` |
| LCD 设置页直接落盘 | `qibiji/HARDWARE/Lcd12864/Dispaly.c:520-551` |
| 云端配置带校验落盘 | `qibiji/HARDWARE/GPRS/gprs_JSON.c:101-114, 194-215` |
| 远端配置与本地设置的地址不重叠 | `reg.c:203-225` vs `Dispaly.c:531,550` vs `gprs_JSON.c:201,212` |
| `Witeflag` 全部写者（Grep 全库确认仅两处） | `switch.c:168, 201` |
| `EepromAddr` 全部赋值（确认无 `0x0005`） | `reg.c:15`（初值 0）、`switch.c:169`（0x0006）、`switch.c:202`（0x0004） |
