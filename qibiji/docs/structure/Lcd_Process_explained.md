# Lcd_Process() 代码解读

> **源码位置**：`qibiji/HARDWARE/Lcd12864/Dispaly.c` L188–L192（本体）
> **配套文件**：`Display.h`、`Lcd12864.C/.h`（底层驱动）、`DispalyPara.c`（数字格式化）、`USER/main.c`、`HARDWARE/KEY/key.c/.h`、`HARDWARE/TIMER/timer.c`、`HARDWARE/DC_MOTOR/dc_motor.c`、`HARDWARE/GPRS/gprs_AT.c`、`HARDWARE/4G/4G.c`、`HARDWARE/REG/reg.c/.h`、`HARDWARE/GPRS/net_liveness.c`
> **文档性质**：代码解读笔记，**未修改任何源码**。行号基于 2026-11-04 的工作副本。
> **姊妹文档**：`docs/ADC1_Read_filter_explained.md`（电流数据从哪来）、`docs/Dcmotor_Process_explained.md`（按键的另一半消费者）

---

## 0. 一句话总览

`Lcd_Process()` 本体只有 4 行（L188–L192）：

```c
void Lcd_Process()
{
    BLight_Ctrl();   // 背光管理
    LcdDisplay();    // 内容刷新
}
```

它是 12864 液晶的**主循环入口**，无节拍门控，每轮超循环都执行。真正的内容是一套三层结构：

> **驱动层**（`Lcd12864.C`，模拟串行时序写 ST7920）→ **格式化层**（`DispalyPara.c` + 16 字节文本行缓冲）→ **调度层**（`Dispaly.c`，页面状态机 + `DisplayCache` 变化检测）。

最容易被误读的两个设计：

1. **"事件驱动刷新"其实每 500ms 强制全量重绘一次**——L329–L333 的 `if (refresh_cnt >= 10)`（5 秒兜底）被注释掉了，只剩 500ms 周期。但这不是 bug 残留：**设置页的编辑位闪烁靠它驱动**，删掉闪烁就死。
2. **LCD 页面状态反过来控制电机按键**：`dcmotor_keycheck()`（`dc_motor.c` L256）用 `PageSta == 0` 判断"是否在首页"来决定 KEY_UP/DOWN/ENTER 是控制电机还是控制菜单，而 `PageSta` 正是由 `Lcd_Process()` 写的。显示模块和电机模块通过这个字节形成了**双向耦合**。

---

## 1. 调用上下文

### 1.1 主循环位置（`main.c` L58–L84）

```c
while (1)
{
    IWDG_Feed();
    sbf20_poll();                 // 蓝牙
    if (uart3_at_sequence_main_loop_tick()) continue;
    KEY_Process();                // KEY      → 产生 KeyNum
    AD_Process();                 // 采集      → armReg.motor_curr / g_adc_val[]
    Dcmotor_Process();            // 电机      ← 消费 KeyNum（看 PageSta）
    Lcd_Process();                // 液晶显示  ← 本文主角，消费 KeyNum（翻页/编辑），写 PageSta
    PropertyReport_Process();
    Gprs_Process();               // 4G
    Eeprom_Wpara();
    manage485();
}
```

要点：

- **顺序有因果**：`AD_Process()` 先更新电流/电压 → `Dcmotor_Process()` 可能改 `sys_moter_state` → `Lcd_Process()` 紧随其后，本轮画的就是**刚发生**的动作，显示零延迟。
- **`KeyNum` 是"单帧脉冲"**：`KEY_Process()`（`key.c` L37–L41）先 `Get_KeyVal()`（10ms 消抖、边沿检测）再 `KeyNum = KEY_Scan()`，`KEY_Scan()` 从 `keyvalue[]` 取走后清零（L44–L59）。所以一次按键只让 `KeyNum` 非零**一轮**，`Dcmotor_Process()` 和 `Lcd_Process()` 在同一轮里读到的是同一个值。
- **初始化**：`main.c` L42 `Lcd_Init()`——GPIO 初始化 → 分三段共 1.5s 等屏幕上电（每段 500ms 喂一次狗，避免越过 3.18s 看门狗窗口）→ 清 GDRAM → `DisplayCache_Init()` → 画首页。

### 1.2 节拍来源（`timer.c`）

| 标志 | 置位条件 | LCD 中的用途 |
|---|---|---|
| `flag300ms` | `cnt % 60 == 0` → `= 0x03` | **bit0** → `BLight_Ctrl()` 背光计时（L206），消费后 `&= 0xfe` |
| `flag500ms` | `cnt % 100 == 0` | `LcdDisplay()` 兜底刷新 + 编辑位闪烁节拍（L325），消费后清零 |

`flag300ms` 一个字节两个位服务两个消费者（bit0 → LCD 背光，bit1 → `dcmotor_auto()`），各自消费各自清位，互不干扰。

### 1.3 显示数据的上游（谁在喂 LCD）

| 显示内容 | 数据源 | 写入位置 |
|---|---|---|
| 电机电流/电压 | `armReg.motor_curr`（500ms 滑动平均，mA）、`g_adc_val[2]` | `ADC1_Read_filter()`（ADC 文档 §8） |
| 开度/状态字 | `armReg.gate_kaidu`、`sys_moter_state`、`FLAG_OPENALL/CLOSEALL` | `timer.c JudeKaidu()`、电机链路 |
| 信号强度 | `xinhao[6..7]` | **`gprs_AT.c` L371–372 直接写 LCD 的显示缓冲**（`handleAT_CSQ`） |
| CCID | `ccid[6..15]` | **`gprs_AT.c` L412–421 直接写**（`handleAT_ICCID`，含 9 位省略形式） |
| 在线状态 | `gprsonlineflag`（`4G.c` L121/L131，PC5 中断里置位）+ `NetLiveness_IsServerConfirmed()`（`net_liveness.c` L120） | 三态：离线/在线/未确认 |
| OTA 允许 | `OTA_Enable`（`reg.c` L18，云端可配） | 设备页 |
| 堵转阈值、闸位最大值 | `armReg.motor_warning_curr_upper`、`armReg.gate_maxheight` | 本地设置页写入 → EEPROM |

注意 `xinhao[]`/`ccid[]` 这两个数组：它们是 `Dispaly.c` 的全局显示缓冲，却被 `gprs_AT.c` **跨模块直接填数**——LCD 模块把自家显示缓冲当成了模块间数据接口，省了一层拷贝，代价是强耦合（改名/改布局要动两个文件）。

---

## 2. 硬件与驱动层（`Lcd12864.C`）

### 2.1 电气连接（`Lcd12864.h` L8–L10、`Dispaly.c` L162–L185）

12864 采用 ST7920 类控制器，**串行模式**（PSB 接地），三线模拟时序：

| 信号 | 引脚 | 说明 |
|---|---|---|
| `CS` | PE3 | 片选 |
| `SID` | PE4 | 数据线 |
| `SCLK` | PE5 | 时钟线 |
| `BackLight` | **PA0** | 背光控制（`Display.h` L16 `PAout(0)`） |

注释提醒：液晶驱动电压 5V，STM32 必须用 5V 兼容 IO（选的 PE 口满足）。

### 2.2 写时序（L15–L57）

`WRCommandH`/`WRDataH` → `SPIWH(Wdata, WRS)` → `SendByteLCDH()`。串行帧格式：先发 `0xF8 + (WRS << 1)`（WRS=0 命令 / 1 数据），再把一个字节拆成**高 4 位、低 4 位**两帧发送，每帧 8 个 SCLK 边沿。`SendByteLCDH` 里 `(WLCDData << i) & 0x80` 是"移位取最高位"的写法，实际是 MSB 先行。每条命令后 `delay_us(100)`、每字节数据 `delay_us(20)`——这是纯阻塞 IO，刷新一整屏文本约几 ms 级，主循环能承受。

### 2.3 三层显示结构

ST7920 有两套显示 RAM，本工程两层都用：

1. **文本层（DDRAM）**：按 GB2312 编码写字节对即显示汉字/ASCII。12864 每行 8 个 16×16 汉字，四行的**行首地址不连续**：

   | 行 | DDRAM 地址 | 本工程用途 |
   |---|---|---|
   | 1 | `0x80–0x87` | 被 GDRAM 绘图层占用（进度条 + 标题点阵） |
   | 2 | `0x90–0x97` | 第二数据行（开度/闸位/信号/编号/限位） |
   | 3 | `0x88–0x8F` | 第三数据行（电机/堵转/CCID/版本/限位） |
   | 4 | `0x98–0x9F` | 第四数据行（流量/校准/状态/升级） |

   这就是代码里 `ShowQQCharH(0x90, ...)`、`(0x88, ...)`、`(0x98, ...)` 的含义——**0x88 是第三行**不是第一行，第一次读很容易搞混。

2. **绘图层（GDRAM）**：128×64 点阵，`WRCommandH(0x34/0x36)` 进扩展指令集后按坐标写。本工程用它做三件事：
   - `CLEARGDRAMH()`（L164）：开机全清；
   - `WRGDRAM(x, &code[..])`（L198）：把 24 字节的 16×16 手工点阵画到标题区（`code[]` 数组存"控制/配置/电池电机/流量报警/网络/设备/远程地址/限位/OTA 参数"等字模，L77–L115）；
   - `GenerateProgressBar1(curr, all, segment)`（L369）：把 0–127 像素按 `curr/all` 比例点亮，画在 **GDRAM 第 14 像素行（Y=14，L499 `WRCommandH(0x80+14)`）**——这就是每个页面顶部那条**页码进度条**（5 页中第 N 页），输出缓冲复用全局 `xianshi[16]`；紧挨着的 Y=15（L189 `WRGDRAM_line()`）写整行全 0xFF 边框，两者叠成「一条整线 + 贴在上方的一段亮条」。**注意**：L351–L352 的 `WRCommandH(0x80)`（Y=0）属于**旧版** `GenerateProgressBar()`（L293–L358），该函数全库无调用方（见 §11 第 11 条），是死代码。

3. **反白高亮**：12864 硬件有整体反色指令但精度太粗，本工程用**往 GDRAM 对应区域整块写 0xFF** 的方式做行/局部反白：
   - `WRGDRAM128X8(x1, y1, d1)`（L129）：16 行 × 8 字节对，覆盖一整行文本区域（约 128×16 像素）；
   - `WRGDRAM128X3(x1, y1, d1)`（L146）：只写 3 字节对（48 像素宽），用于操作页"上限位/下限位"**局部**反选。
   - `d1 = 0xff` 反白、`0x00` 恢复——设置页选中的那一行反白、其余行清零，就是靠连续调三次 `WRGDRAM128X8` 实现的。

### 2.4 补充：「反选」语义、GDRAM 行号与进度条的两层分段

**（1）「反选」= 反白高亮，不是「取消选择」。** 这里指 inverse highlight：把选中那一行的前景/背景黑白对调，做成一条深底浅字的高亮带，用途是告诉用户「设置页光标落在哪一项」（由 `SetPageNum` 管）。中文 UI 里「反选」还有 unselect 的意思，本工程**不是**那个含义。

**（2）注释是坐标标签，不是动作描述。** 全项目 12 处 `WRGDRAM128X8` 调用的注释一字不改都写「第二行反选 / 第三行反选 / 第四行反选」，它标的是「这一句负责哪一行的反选状态」，至于设成选中还是清除，要看第三个参数 `d1`。例如 `WRGDRAM128X8(0, 0x10, 0x00)` 应读作「**把第二行的反选清掉**」；真正点亮高亮的只有 `setpage()` 里那三个 `0xff` 分支（`Dispaly.c:693/702/714`），`DisplayViewPages()` 的 case 0/2/3 三行全 `0x00` 是切回页面时擦残留。

**（3）三个参数与「第几行」的对应**（`Lcd12864.C:129-144`）：

| 参数 | 含义 |
|---|---|
| `x1` | GDRAM 水平起始地址：`0` = 上半屏、`8` = 下半屏 |
| `y1` | 半屏内垂直起始行：`0` 或 `0x10`(16) |
| `d1` | 每个要写的字节：`0xff` 反白、`0x00` 清除 |

12864 是 128×64 点阵、一个汉字行占 16 像素，共 4 行；但 ST7920 的 GDRAM **分上下两个半屏**，下半屏的 Y 要重新从 0 数，所以：

| 屏幕行 | 像素行 | `(x1, y1)` | DDRAM 地址 | 首页内容 |
|---|---|---|---|---|
| 第 1 行 | 0~15 | `(0, 0x00)` | `0x80` | 标题（从不反选） |
| 第 2 行 | 16~31 | `(0, 0x10)` | `0x90` | 开度 / 闸位 |
| 第 3 行 | 32~47 | `(8, 0x00)` | `0x88` | 电机 / 堵转 |
| 第 4 行 | 48~63 | `(8, 0x10)` | `0x98` | 流量 / 校准 |

一次调用 = 16 个 Y 行 × 8 次 × 2 字节 = 256 字节，正好盖满一整行（名字 `128X8` 即「128 点宽 / 内层写 8 次」）。`WRGDRAM128X3`（L146）是同一套的局部版，只写 48 点宽，给「上限位/下限位」两个词做小块反白。

**（4）进度条的两层「段」不是一回事。** `GenerateProgressBar1(curr, all, segment)` 里：

| | 5 段（逻辑份） | 8 段（物理格） |
|---|---|---|
| 层面 | 业务语义 | 硬件 / 存储 |
| 谁定 | 参数 `all`，当前恒为 5（分给 5 页） | ST7920 一次写 2 字节 = 16 点，`xianshi[16]` 固定 8 格 |
| 每段宽度 | `128/5 ≈ 25.6` 点 | 16 点 |
| 代码 | L391-400 算 `[start,end]` | L421 `x/16`、L471/L501 `i<8` |
| 会不会变 | 加页就变 | 永远 8 |

`128/5 ≈ 25.6 点` 比一个物理格 16 点还宽，所以每个逻辑页必然横跨 2~3 个物理格（页 1 段 0→1、页 2 段 1→3、页 3 段 3→4、页 4 段 4→6、页 5 段 6→7），**同段分支（L427-451）恒不执行**，跨段分支才干活；把 `all` 改成 ≥8 才可能落进同一格，那时 `0xFF >> 8 = 0` 的 bug 才会暴露。这补上了 §11 第 11 条「旧版死代码」之外的另一处死分支。

**（5）它是分页指示器，不是随数值增长的进度条。** 五个页面各自把 `curr` 硬编码成 1~5、`all` 固定 5，每页只亮一段，与 `ViewNum + 1` 没有关联；加第 6 页要连改 5 处。页面调度与各 `curr` 调用点见 `docs/ViewPages_explained.md`。

---

## 3. 数据结构总表

### 3.1 全局状态变量（`Dispaly.c` L15–L16、L118–L122）

| 变量 | 类型 | 含义 | 谁读 |
|---|---|---|---|
| `autoFlag` | `u8` | 自动校准开关 | `dcmotor_auto()` 消费（**又一个显示模块导出给电机的变量**） |
| `setParaflag` | `u8` | 设置页"编辑中"标志 | 仅 `setpage()` |
| `HomeNum` | `u8` | 首页项目数（旧版遗留，现无按键处理它） | — |
| `ViewNum` | `u8` | **当前页面号 0–4**（0 首页/1 设置/2 4G/3 设备/4 操作） | `LcdDisplay`/`dcmotor_keycheck` 间接 |
| `SetPageNum` | `u8` | 设置页内选中项 0–2（0xff = 未选中） | `setpage()` |
| `SetParaNum` | `u8` | 设置参数项目数（遗留，未用） | — |
| `PageSta` | `u8` | **页面号的对外副本**：`ViewNum==0 → 0`，否则 `= ViewNum` | `dc_motor.c L256 dcmotor_keycheck()` |

`ViewNum` 与 `PageSta` 是同一个信息的两份表达：前者是内部导航态，后者是给电机模块看的"我是否在首页"信号（`ViewPages()` L384–L389 每次刷新后同步）。

### 3.2 文本行缓冲（L29–L68）——16 字节的"行模板"

每个显示行是一个 16 字字节数组，初始化时写满 GB2312 默认文本，动态部分**原地改写后整行重发**。以 `motor[16]` 为例：

```
索引:  0    1    2    3    4    5    6    7    8    9    10   11   12   13   14   15
内容: [电  d5a7][机  bbf a][':' 3a][' ' 20]['1' 31]['.' 2e]['6' 36]['A' 41][' ' 20]['1' 31]['2' 32]['.' 2e]['6' 36]['V' 56]
      ├──── 静态标题 ────┤ ├──── DisplayFloat 写 ────┤      ├──── DisplayFloat 写 ────┤
```

- `DisplayFloat(&motor[5], armReg.motor_curr/100, 0)` 只覆盖 [5..8]，[9] 的 `'A'` 是模板自带的单位；
- `DisplayFloat(&motor[11], g_adc_val[2], 0)` 覆盖 [11..14]，[15] 的 `'V'` 同理。

**单位换算链**：`armReg.motor_curr` 内部是 mA（500ms 滑动平均）→ `/100` 变成 0.1A 单位 → `DisplayFloat` 按"2 位整数 + 1 位小数"拆字符 → 屏上 `1.6A`。电压 `g_adc_val[2]` 内部是 0.1V → 屏上 `12.6V`。

同族的缓冲：`kaidu`（开度+状态字）、`liuliang`（流量，静态）、`zhaweimax`（闸位最大值，可编辑）、`duzhuan`（堵转电流，可编辑）、`jiaozhun`（校准状态）、`xinhao`/`ccid`（4G 页，被 `gprs_AT.c` 远程填数）、`order`/`banben`/`shengji`/`dstate`/`PV`/`empty`/`shang_xia_xianwei`。

### 3.3 DisplayCache——变化检测缓存（`Display.h` L19–L34，`Dispaly.c` L128–L144）

全部字段初始化为"不可能值"（0xFF/0xFFFF），保证首次比较必然不等、必然刷新一次。字段与页面一一对应：

| 页面 | 监测字段 | 触发刷新的变化 |
|---|---|---|
| 全局 | `page_last` | 页面号变了（切页强制刷） |
| 首页 0 | `motor_curr_last`/`motor_vol_last`/`kaidu_last`/`motor_state_last` | 电流、电压、开度、电机状态任一变化 |
| 设置 1 | `gate_max_last`/`motor_warning_curr_last`/`autoFlag_last` | 闸位最大值、堵转阈值、校准开关变化 |
| 4G 2 | `gprsonlineflag_last`/`serverok_last`/`signal_last[2]`/`ccid_last[10]` | 在线态、平台确认、信号、CCID 变化 |
| 设备 3 | `PV_last` | 光伏电压变化（**但页面对应显示已被注释，空转**） |
| 操作 4 | `positionState_last` | 限位状态变化 |

注意 `kaidu_last` 是 `u8` 而 `armReg.gate_kaidu` 是 `u16`（`reg.h` L47）——比较发生隐式截断；开度实际 ≤100 无害，但类型不严谨。

---

## 4. BLight_Ctrl()——背光管理（L195–L217）

```c
if (KeyNum)            { BackLight = 1; cnt = 0; }      // 任意键点亮并重置计时
else if (flag300ms & 0x01) { flag300ms &= 0xfe;         // 每 300ms 计一次
    cnt++;
    if (cnt >= 100)   { BackLight = 0; cnt = 0; } }     // 100 × 300ms = 30s 熄灭
```

- `KeyNum` 是单帧脉冲（见 §1.1），所以**按下那一轮**背光就点亮，同轮 `LcdDisplay()` 也跟着强制刷新（L248–L251 有键必刷）——按键 → 亮屏 → 立即重绘，一气呵成。
- 熄灭周期是 **30 秒**（100 × 300ms），而 L25 的注释写"60s 无操作返回到首页并关闭背光"——**注释与实现不符**（时间对不上，且"返回首页"的逻辑不存在，见 §11）。
- 背光极性存在**注释自相矛盾**：L184 `BackLight = 1; // 关闭背光`（初始化）vs L201 `BackLight = 1; // 打开背光`（按键点亮）。同一电平两处注释相反，必有一处写错；从 `BLight_Ctrl` 的行为逻辑看更可能是初始化处注释错（上电默认亮屏更合理），**真实极性以原理图为准**。

---

## 5. LcdDisplay()——五路触发的事件驱动刷新（L231–L343）

函数每轮主循环都进来，靠 `need_refresh` 一个局部标志聚合五个触发源，只在需要时才调用 `ViewPages(KeyNum)` 真正刷屏：

1. **页面切换**（L242–L245）：`displayCache.page_last != ViewNum` → 强制刷。`page_last` 在刷新完成后回写（L341），所以本条件一轮内只触发一次。
2. **按键事件**（L248–L251）：`KeyNum` 非零 → 强制刷。
3. **数据变化**（L254–L322）：按当前页面只比较该页关心的字段（§3.3 表）。**比较与回写放在一起**——只有确认要刷新才更新缓存，避免"缓存更新了但刷新被更高优先级逻辑跳过"的错位。
4. **500ms 定时兜底**（L325–L334）：

```c
if (flag500ms) {
    flag500ms = 0;
    refresh_cnt++;
    // if (refresh_cnt >= 10)   // 10 × 500ms = 5秒  ← 被注释
    // {
        need_refresh = 1;
        refresh_cnt = 0;
    // }
}
```

**这是整个函数最值得看清的地方**：注释文字说"降低频率，仅作为兜底机制"，但 5 秒门限那两行被注释掉了，实际效果是**每 500ms 无条件强制全量刷新**。所谓"事件驱动"退化为周期刷新——数据变化检测（第 3 路）最多把刷新提前几百 ms，其余时间都在做重复劳动（每次刷新 = 重写整行文本 + 反白区，纯阻塞 SPI）。

但**不能简单"修复"它**：`setpage()` 的编辑位闪烁（§8.3）依赖 `flag` 在每次进入 `setpage()` 时翻转，而进入 `setpage()` 的唯一周期性入口就是这个 500ms 兜底——**它是闪烁的节拍源**。删掉兜底 = 编辑光标不再闪；恢复 5 秒门限 = 闪烁变成 5 秒一跳。正确的修法是闪烁用独立节拍、兜底恢复低频，两者解耦。

5. **刷新执行**（L337–L342）：`ViewPages(KeyNum)` → 回写 `page_last`。

---

## 6. ViewPages()——页面导航与按键分发（L346–L391）

```c
uint8_t ViewPages(uint8_t KeyNum)
{
    switch (KeyNum) {
    case KEY_LEFT:  ViewNum > 0 ? ViewNum-- : (ViewNum = View_Num);  // 0 → 4，环绕
    case KEY_RIGHT: ViewNum < View_Num ? ViewNum++ : (ViewNum = 0);  // 4 → 0
    case KEY_ENTER: if (ViewNum != 1) { ViewNum = 0; }               // 回首页，设置页除外
    }
    DisplayViewPages(ViewNum);      // 真正画页面
    PageSta = (ViewNum == 0) ? 0 : ViewNum;   // 同步给电机模块
    return ViewPage;               // 恒返回 0（#define ViewPage 0），无人消费
}
```

三个细节：

- **KEY_ENTER 在设置页（ViewNum==1）不回首页**：因为设置页需要 ENTER 做"选中项/确认"（§8），导航语义让位给编辑语义。在 4G/设备/操作页按 ENTER 则直接回首页。
- **`PageSta` 的同步在 `DisplayViewPages()` 之后**，即"页面真画完了"才更新对外状态。
- `return ViewPage` 恒为 0 且无调用方使用返回值——遗留写法。

### 6.1 与电机模块的按键耦合（本函数最重要的副作用）

`dcmotor_keycheck()`（`dc_motor.c` L254–L272）的判据是：

```c
if (PageSta == 0) {          // 只在"首页"时，按键才控制电机
    KeyNum == KEY_UP   → sys_moter_state = MOTOR_UP;
    KeyNum == KEY_DOWN → sys_moter_state = MOTOR_DOWN;
    KeyNum == KEY_ENTER→ sys_moter_state = MOTOR_STOP;
}
```

主循环顺序是 `Dcmotor_Process()` **在前**、`Lcd_Process()` **在后**，所以存在**一拍延迟**：

| 轮次 | KeyNum | dcmotor_keycheck 看到的 PageSta | 结果 |
|---|---|---|---|
| 第 N 轮（按 LEFT 离开首页） | KEY_LEFT | 0（上一轮的值） | 不匹配 UP/DOWN/ENTER，无电机动作 ✓ |
| 第 N 轮 Lcd_Process | — | — | ViewNum=1，PageSta=1 |
| 第 N+1 轮（按 UP 调参数） | KEY_UP | 1 | **不动电机** ✓ |
| 第 N+1 轮 Lcd_Process | — | — | `setpage()` 把 KEY_UP 用于改数字 ✓ |

由于翻页键（LEFT/RIGHT）与电机键（UP/DOWN/ENTER）**键值不重叠**，这一拍延迟恰好无害；切换生效后，同样的 KEY_UP/DOWN 在设置页只改参数、在操作页什么都不做。整个"同一组按键，页面决定语义"的分发，就靠 `PageSta` 这一个字节兜住。

反向耦合同样存在：`autoFlag`（`Dispaly.c` L15 定义）在设置页被置位（L562/L566），由 `dcmotor_auto()`（`dc_motor.c` L125）消费驱动自动校准——**显示模块导出了两个控制量给电机模块**（`PageSta`、`autoFlag`）。

---

## 7. 五个页面逐个讲（DisplayViewPages L394–L433 分发）

### 7.1 首页 homepage()（L436–L467）

- **静态部分只画一次**：`page_init_flag == 0 || page_last != 0`（首次或从别的页切回来）时才 `LCDInit()` + 画标题点阵（"控制"两字，`WRGDRAM(3/4, ...)`）+ 边框 + 进度条 `GenerateProgressBar1(1, 5, xianshi)`。
- **动态部分每次刷新都写**：电流 `DisplayFloat(&motor[5], ...)`、电压 `DisplayFloat(&motor[11], ...)`、开度+状态字 `kaiduVal(&kaidu[6], &kaidu[12])`，最后三行整行 `ShowQQCharH` 重发。
- `kaiduVal()`（`DispalyPara.c` L94–L181）同时干两件事：把开度数字按 1/2/3 位宽**右对齐**写进 `kaidu[6..8]`（>100 钳位成 "100"）；再按 `sys_moter_state` 写状态字——UP→"上升"、DOWN→"下降"、STOP→"全开/全关/停止"（看限位标志）。**开度数字和状态字共用一次函数调用**，这是 `kaidu` 缓冲里 `[6..8]` 数字、`[12..15]` 状态字分开传指针的原因。
- 首页无编辑项，`DisplayViewPages` case 0 里紧跟三条 `WRGDRAM128X8(..., 0x00)` 把二三四行反白清掉（从设置页回来时把选中高亮擦干净）。
- 第四行"流量"是**纯静态文本**（`liuliang` 缓冲从没被动态更新过，一直显示 "0.0 m3/s"）——预留位，当前版本无流量传感器数据。

### 7.2 4G 页 G4gpage()（L724–L751）

标题"网络"点阵 + 进度条 3/5。动态三行：信号（`xinhao`，AT+CSQ 解析结果直填缓冲）、CCID（`ccid`，9 位省略形式 + 自插的 `'-'`，详见 ICCID 那轮的讨论）、状态（`DisplayState()`，`DispalyPara.c` L213–L233）。`DisplayState` 是**三态**显示：

| 条件 | 显示 | 语义 |
|---|---|---|
| `!gprsonlineflag` | 离线 | 模块连注网都没完成（PC5 LINK 电平判定） |
| 在线 && `NetLiveness_IsServerConfirmed()` | 在线 | 心跳收到平台回执，链路真通 |
| 在线 && 未确认 | 未确认 | 模块注网了，但心跳回执还没来 |

这个三态和 `net_liveness.c` 的探活分级自愈策略是对着设计的——屏上"未确认"就是云端开始丢事件的预警。

### 7.3 设备页 Devicepage()（L754–L785）

标题 "OTA 参数" 点阵 + 进度条 4/5。静态：编号/版本/升级三行。`DisplayOTAenable()`（`DispalyPara.c` L236–L253）按 `OTA_Enable` 显示"允许/禁止"。

**两个问题**：① 它只在页面初始化分支里被调用，`OTA_Enable` 被云端改掉后**不刷新**（要切页再回来才更新）；② `LcdDisplay` 的 ViewNum==3 分支监测 `PV_last`（光伏电压），但页面里对应的 `DisplayFloat(&PV[6], ...)`/`ShowQQCharH(0x98, PV, 8)` 全被注释（L782–L784）——**监测空转**，变化检测触发的刷新画不出任何新东西。

### 7.4 操作页 OperatePage()（L788–L847）

标题"限位"点阵 + 进度条 5/5。显示"上限位  下限位"文本，按 `FLAG_OPENALL/FLAG_CLOSEALL` 用 `WRGDRAM128X3` 做**局部**反白：到上限位反白"上限位"三字，到下限位反白"下限位"。这是给现场调试员看的**限位开关实时指示**。

`positionState` 的计算逻辑在这里出现一次（L794–L805），在 `LcdDisplay` ViewNum==4 分支又出现一次（L311–L321）——**同一段三态映射写了两遍**，改判定时要记得改两处。

---

## 8. setpage()——设置页编辑状态机（L470–L721，全文件最复杂）

### 8.1 两个模式、三个选中项

```
非编辑态 (setParaflag == 0)              编辑态 (setParaflag == 1)
┌────────────────────────────┐      ┌────────────────────────────┐
│ KEY_UP/DOWN: 移动 SetPageNum │      │ KEY_UP/DOWN: 改数字 Num     │
│   0=闸位最大值 (第二行)      │ ───► │   '0'..'9' (0x30..0x39)   │
│   1=堵转电流   (第三行)      │ ENTER│ KEY_ENTER: 确认当前位,     │
│   2=校准开关   (第四行)      │ ◄─── │   cnt 前进一位             │
│ KEY_ENTER: 先选中(0xff→0),   │ 完成 │ 编辑满自动退出+落盘         │
│   再按进入编辑               │      │                            │
└────────────────────────────┘      └────────────────────────────┘
```

页面切换初始化（L480–L498）把 `SetPageNum = 0xff`（未选中）、`setParaflag = 0`。**首次 ENTER 只是选中第一项**（L661–L663：`0xff → 0`），**第二次 ENTER 才进入编辑**（L664–L665：`setParaflag = 1`）。

选中行高亮：`SetPageNum` 变化时（L685–L719）把对应文本行的 GDRAM 区块写 0xff 反白、其余两行写 0x00 恢复。

### 8.2 三个编辑项的处理差异（KEY_ENTER，L520–L571）

| 选中项 | 编辑缓冲 | 编辑位数与特殊处理 | 完成后落盘 |
|---|---|---|---|
| 0 闸位最大值 | `zhaweimax[6..9]` | 4 位数字（mm），cnt 6→10 | `armReg.gate_maxheight = asciitoInt(&zhaweimax[6], 4)`；`AT24CXX_WriteWord(0x0006, ...)` |
| 1 堵转电流 | `duzhuan[6..9]` | 3 位数字，`cnt==8` 时 `cnt++` **跳过小数点**（[8]='.'），屏显 "XX.X" A | `asciitoInt(&duzhuan[6], 4) × 100`（×100 变 mA）→ 限幅 0xFFFF → `armReg.motor_warning_curr_upper`；`AT24CXX_WriteWord(0x0004, ...)` |
| 2 校准开关 | `jiaozhun[6..13]` | 不进数字编辑：KEY_UP=`row3flag=1`(开)、KEY_DOWN=`0`(关)，ENTER 确认 | `autoFlag = row3flag`；`fjiaozhun()` 显示"自动执行/关闭" |

**项 1 就是堵转保护的现场整定入口**：屏显 "35.0" A → 内部 3500 mA → 正是 `ADC1_Read_filter()` 堵转判定比较的那个阈值（ADC 文档 §9）。`row3flag` 是个复用变量：在编辑态的项 0/1 里无意义，只在项 2 里承载开/关。

### 8.3 编辑位闪烁机制（L577–L640）——为什么 500ms 兜底删不得

编辑态的显示分支每次进入时翻转局部 `static flag`：

```
第 1 次进入 setpage:  flag=1 → zhaweimax[cnt] = Num   （显示数字）
第 2 次进入 setpage:  flag=0 → zhaweimax[cnt] = 0x20  （显示空格）
```

`setpage()` 的唯一周期性入口是 `LcdDisplay()` 的 500ms 兜底刷新 → `ViewPages()` → `DisplayViewPages(1)` → `setpage()`。所以**闪烁周期 = 2 × 500ms = 1s**，视觉上就是一个 1Hz 的编辑光标。数据变化检测那一路基本不会触发（编辑时电机静止、电流不变），兜底实际就是闪烁的**唯一驱动**。

### 8.4 asciitoInt() 的宽容解析（`DispalyPara.c` L10–L25）

```c
for (i = 0; i < len; i++)
    if ('0' <= src[i] <= '9') { val = val + (src[i] - '0'); val = val * 10; }
val = val / 10;   // 修掉多乘的一次
```

数字字符"加完即乘"，最后整体除 10 校正。**非数字字符直接跳过**（不加也不乘）——所以 `"12.6"` 解析出 126（小数点被无视），这正是设计意图：屏面格式 `"XX.X"` 直接映射内部 0.1A 刻度。副作用是任意乱字符都被静默吞掉，没有错误反馈。

---

## 9. 参数落盘与 EEPROM 地址语义

`setpage()` 编辑完成时**直接写** `AT24CXX_WriteWord(0x0004/0x0006, ...)`，绕过了 `Eeprom_Wpara()` 的命令码机制。这里有个必须分清的**同名异物**：

| 语境 | `0x0004` 的含义 | 依据 |
|---|---|---|
| `setpage()` L531/L550 的实参 | **EEPROM 物理地址**，对应 `motor_warning_curr_upper` | 与 `reg.c` L236–L237 开机全量初始化的物理地址一致 ✓ |
| `Eeprom_Wpara()` L208 的 `case 0x0004` | **命令码**，对应动作"写物理地址 0x0008（gate_maxturns）" | `reg.c` L210–L213 |

`EepromAddr` 是"哪类参数要写"的命令码（case 0x0004→写 0x0008，case 0x0005→写 0x000A，case 0x0006→写 0x000C），物理地址在 case 内部另算。**命令码恰好和某些物理地址撞值**，两套语义并存极易写混——姊妹文档 `Dcmotor_Process_explained.md` §11 存疑的 "switch.c:202 校准写 EepromAddr=0x0004 疑与电流阈值冲突"，实际就是撞了命令码语境：那个 0x0004 走的是 `Eeprom_Wpara()` 的 case，写的是 0x0008（圈数），**不会**覆盖电流阈值。存疑可解除，但建议给命令码改名（如 `EE_CMD_xxx`）防再犯。

物理地址全表（`reg.c` L236–L240）：`0x0004` 堵转电流阈值、`0x0006` 闸位最大值、`0x0008` 最大圈数、`0x000A` 开度、`0x000C` 开度计数。

---

## 10. 上电时间线

```
main() L42  Lcd_Init()
  ├─ Lcd_Gpio_Init(): PE3/4/5 + PA0，BackLight = 1（注释"关"，见 §4 疑点）
  ├─ delay 500ms → 喂狗 → 500ms → 喂狗 → 500ms → 喂狗     共 1.5s，等屏幕启动
  ├─ CLEARGDRAMH(0x00)         清绘图层
  ├─ DisplayCache_Init()       所有 *_last = 0xFF（保证首刷）
  └─ homepage()                page_init_flag==0 → 画静态+动态，首页上线
     （此时电流显示为 0：ADC 零点 320ms 未完成，motor_curr 滑动窗口也未满，
       约 1s 内首页电流/开度会跳变到真值——见 ADC 文档 §10 时间线）
```

之后每轮主循环 `Lcd_Process()` 无门控执行；首屏到第一次"正确数据"之间靠 500ms 兜底刷新自然收敛。

---

## 11. 静态阅读发现的问题（仅记录，未修改）

| # | 位置 | 问题 | 影响 |
|---|---|---|---|
| 1 | `Dispaly.c` L329–L333 | 500ms 兜底的 5 秒门限被注释，"事件驱动"退化为每 500ms 全量重绘 | SPI 阻塞写每秒 2 次全屏文本+反白，主循环被占几 ms；但它同时是闪烁节拍源（§8.3），**不能直接恢复注释**，需先给闪烁独立节拍 |
| 2 | `Dispaly.c` L184 vs L201 | `BackLight = 1` 两处注释一"关"一"开"，自相矛盾 | 极性以原理图为准；至少一处注释错 |
| 3 | `Dispaly.c` L25 vs L195–L217 | 注释"60s 无操作返回首页并关闭背光"，实现是 **30s** 且**无返回首页逻辑** | 注释误导；若需要"超时回首页"防呆，得补 `ViewNum = 0` |
| 4 | `Dispaly.c` L754–L785 | 设备页动态数据全被注释，但 ViewNum==3 的 `PV_last` 变化检测仍在跑 | 检测空转；`DisplayOTAenable` 只在初始化分支调用，`OTA_Enable` 云端变更后屏不更新 |
| 5 | `Display.h` L23 vs `reg.h` L47 | `kaidu_last` 是 u8，`armReg.gate_kaidu` 是 u16，比较隐式截断 | 开度 ≤100 现状无害，改开度量程会踩坑 |
| 6 | `Dispaly.c` L309–L322 vs L788–L805 | `positionState` 三态映射写了两遍 | 改判定要同步两处 |
| 7 | `Dispaly.c` L390 | `return ViewPage` 恒为 0（宏 `#define ViewPage 0`），无消费者 | 死代码 |
| 8 | `Dispaly.c` L118–L121 | `HomeNum`/`SetParaNum` 无任何按键逻辑驱动（旧版首页菜单残留） | 死变量 |
| 9 | `Dispaly.c` L15 + `gprs_AT.c` L371/L412 | `autoFlag` 由显示模块定义导出给电机；`xinhao/ccid` 显示缓冲被 GPRS 模块直接写 | 跨模块耦合，改名/改布局要动多文件；建议集中到 `main.h` 或独立接口层 |
| 10 | `Lcd12864.h` L40–L41 | `lcd_set_invert/lcd_get_invert` 有声明、无定义、无调用 | 未链接不报错，属规划中未实现的反色 API |
| 11 | `Lcd12864.C` L293 | 旧版 `GenerateProgressBar(start, end, segment)` 已无调用方（被 V1 取代） | 死代码 |
| 12 | `Dispaly.c` L188–L192 | `Lcd_Process()` 无返回值说明/无参数，但 `LcdDisplay()` 内 `LcdDisplay→ViewPages→DisplayViewPages` 全链每轮可能刷新，最深处调用栈 7 层（链接器实测 Max Depth 132 字节） | 栈安全，但加嵌套时留意 |

硬件相关（继电器极性、5V 兼容）以原理图为准；EEPROM 命令码与物理地址的区分见 §9。

---

## 12. 三个易混点

1. **`0x88` 是第三行**：ST7920 四行行首是 0x80/0x90/0x88/0x98，**不按数值大小排**。读 `ShowQQCharH(0x88, motor, 8)` 时别当成"第一行附近"。
2. **`ViewNum` ≠ `PageSta` ≠ `SetPageNum`**：`ViewNum` 是页面号（0–4），`PageSta` 是它给电机模块的对外副本，`SetPageNum` 是设置页**内部**的选中项（0–2 或 0xff）。三个变量三层菜单，改代码前先分清在操作哪一层。
3. **`KeyNum` 一轮一个消费者都没有排他性**：同轮 `Dcmotor_Process()` 和 `Lcd_Process()` 都读它，靠 `PageSta` 判据分流（§6.1）；且 `KEY_ENTER` 在首页 = 停机、在设置页 = 选中/确认、在其他页 = 回首页——**同一个键，三种语义，全靠上下文**。

---

## 附：函数索引（行号速查）

| 函数 | 文件:行 | 职责 |
|---|---|---|
| `Lcd_Process` | `Dispaly.c:188` | 主循环入口 = 背光 + 刷新 |
| `BLight_Ctrl` | `Dispaly.c:195` | 按键点亮 / 30s 熄灭 |
| `LcdDisplay` | `Dispaly.c:231` | 五路触发聚合，need_refresh 门控 |
| `ViewPages` | `Dispaly.c:346` | 翻页导航 + PageSta 同步 |
| `DisplayViewPages` | `Dispaly.c:394` | 页面号 → 页面函数分发 |
| `homepage` | `Dispaly.c:436` | 首页（电流/电压/开度/状态） |
| `setpage` | `Dispaly.c:470` | 设置页编辑状态机 |
| `G4gpage` | `Dispaly.c:724` | 信号/CCID/在线三态 |
| `Devicepage` | `Dispaly.c:754` | OTA 参数（动态部分已注释） |
| `OperatePage` | `Dispaly.c:788` | 限位反白指示 |
| `DisplayCache_Init` | `Dispaly.c:128` | 变化检测缓存初始化 |
| `Lcd_Init` / `Lcd_Gpio_Init` | `Dispaly.c:147/162` | 上电初始化（分段喂狗） |
| `ShowQQCharH` | `Lcd12864.C:61` | 按地址写 8 个汉字（整行重发） |
| `WRGDRAM128X8/128X3` | `Lcd12864.C:129/146` | 行反白 / 局部反白 |
| `WRGDRAM` / `WRGDRAM_line` | `Lcd12864.C:198/183` | 点阵图标 / 边框 |
| `GenerateProgressBar1` | `Lcd12864.C:369`（写屏 L499） | 页码进度条（GDRAM Y=14，第 14 像素行） |
| `asciitoInt` / `DisplayInt` / `DisplayFloat` | `DispalyPara.c:10/28/67` | ASCII↔数值、格式化 |
| `kaiduVal` / `fjiaozhun` / `DisplayState` / `DisplayOTAenable` | `DispalyPara.c:94/184/213/236` | 开度+状态字 / 校准字 / 三态 / OTA 字 |
