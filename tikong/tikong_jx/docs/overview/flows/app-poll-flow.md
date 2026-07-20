# **串口通道巡检**流程说明

更新时间：2026-07-18

> **源码**：`User/app_run.c`（声明 `User/app_run.h`）  
> **调用方**：`User/main.c` 主循环（仅在 **未** 跑 4G AT 配网时）  
> **相关**：[at-config-flow.md](../../cloud/4g/at-config-flow.md)（与 AT 互斥）、[qr-process-uart45.md](../../pass/qr-process-uart45.md)、[source-map.md](../../cloud/4g/source-map.md)

---

## 一、一句话结论

`app_poll()` 是日常业务的 **一轮串口通道巡检**：按固定顺序看四路是否有“收完一帧”的标志，有则处理，无则立刻返回。它本身不做 AT 配网，也不直接管限层定时（那两块在 `main` 的 `else` 里、`app_poll()` **之后**）。

---

## 二、何时会被调用

```text
main while(1)
  IWDG_Feed()
  if (g_uart3_at_sequence_request)
      uart3_at_sequence_poll()     ← 配网：不进 app_poll
  else
      app_poll()                   ← 日常
      if (gettimeflag) GetNetTime()
      if (check_limit_time_flag) …限层检查…
```


| 条件                                 | 是否跑 `app_poll` |
| ---------------------------------- | -------------- |
| `g_uart3_at_sequence_request == 0` | **是**          |
| `== 1`（AT 配网中）                     | **否**          |


配网期间读头/云 JSON 看起来“没反应”，是因为主循环把整段业务让给了 AT 状态机。

---

## 三、函数本体（当前源码）

```c
/* User/app_run.c */
void app_poll(void)
{
	PCProcess();        /* USART1 调试口 */
	QRProcessUart5();   /* 读头 UART5 */
	QRProcessUart4();   /* 读头 UART4 */
	G4GProcess();       /* USART3 4G / 云 JSON */
}
```

顺序固定，**一轮内串行执行**；每个 `*Process` 都是“无新帧则空返回”，开销很小。

---

## 四、四路分别做什么

```text
app_poll()
  │
  ├─① PCProcess()          App/Link/pc_comm.c
  │     if (USART_RX_Complete)
  │       打印 RX 内容 → 清标志
  │     （当前不做命令解析）
  │
  ├─② QRProcessUart5()     App/Pass/qr_comm.c
  │     if (UART5_RX_Complete)
  │       拼 "{...}" → type/data/uid
  │       type 0/1 → CommControl(..., 5)
  │       type 2   → 密码处理
  │
  ├─③ QRProcessUart4()     同上，端口 4
  │
  └─④ G4GProcess()         App/Cloud/g4g_comm.c
        if (USART3_RX_Complete)
          parseSerialData(USART3_RX_BUF)   /* 平台 JSON */
          清标志
```


| 调用               | 硬件              | 驱动标志（典型）             | 业务落点                  |
| ---------------- | --------------- | -------------------- | --------------------- |
| `PCProcess`      | USART1 @ 115200 | `USART_RX_Complete`  | 调试回显                  |
| `QRProcessUart5` | UART5 @ 9600    | `UART5_RX_Complete`  | 通行 / 设置 / 密码          |
| `QRProcessUart4` | UART4 @ 9600    | `UART4_RX_Complete`  | 同上                    |
| `G4GProcess`     | USART3 @ 115200 | `USART3_RX_Complete` | `parseSerialData` 物模型 |


驱动层共性：**DMA + IDLE 定帧只置标志**；真正吃数据在上述 `*Process` 里。

---

## 五、与 `app_init` / `main` 其它逻辑的边界


| 函数/位置                        | 职责                                  | 是否在 `app_poll` 内                                 |
| ---------------------------- | ----------------------------------- | ------------------------------------------------ |
| `app_init()`                 | 上电：`loadDataFromEEPROM`、RTC、限层初始    | 否（`main` 里只调一次）                                  |
| `app_poll()`                 | 四路通道轮询                              | —                                                |
| `gettimeflag` → `GetNetTime` | 定时向云要时间                             | 否（`app_poll` 之后）                                 |
| `check_limit_time_flag`      | 定时查限层截止                             | 否（`app_poll` 之后）                                 |
| `Rs485Process`               | RS485 通道处理（`App/Link/rs485_comm.c`） | **当前未在 `app_poll` 中调用**；梯控发令多在权限路径里直接用 USART2 驱动 |


旧流程图若仍画 `Rs485Process` 在主循环轮询，以 **现行 `app_run.c`** 为准。

---

## 六、一轮执行的时间线（概念）

```text
时间 →
  PCProcess      无帧：几乎立即返回；有帧：打印后清标志
  QRProcessUart5 无帧：返回；有完整 JSON：可能进 CommControl（较长）
  QRProcessUart4 同上
  G4GProcess     无帧：返回；有 JSON：parseSerialData（可能较长）
```

注意：某一路业务处理较久时，本轮后面几路会延后；看门狗在 `main` 每圈开头喂狗。断点调试过久仍可能触发 IWDG 复位。详见 [watchdog-iwdg.md](../watchdog-iwdg.md)。

---

## 七、和 AT 配网的关系（再强调）


|           | `app_poll` 日常路               | AT 配网路                     |
| --------- | ---------------------------- | -------------------------- |
| USART3 数据 | 当平台 JSON，进 `parseSerialData` | 当 AT 回复，等 `OK`             |
| 读头        | `QRProcessUart*` 可跑          | 不跑                         |
| 入口        | `app_poll()`                 | `uart3_at_sequence_poll()` |


详见 [at-config-flow.md](../../cloud/4g/at-config-flow.md)。

---

## 八、联调检查清单

1. 确认未卡在 AT：`g_uart3_at_sequence_request == 0`。
2. 读头无反应：对应口 `RX_Complete` 是否置位；JSON 是否成 `{...}`；见 [qr-process-uart45.md](../../pass/qr-process-uart45.md)。
3. 云无反应：USART3 是否有帧；`G4GProcess` 是否打印 `rx->`。
4. 调试口：USART1 是否有 `RX[n]:`（`PCProcess`）。
5. 限层/校时异常：查 `main` 里 TIM 标志分支，不要只在 `app_poll` 里找。

---

## 九、源码索引


| 文件                      | 角色                                  |
| ----------------------- | ----------------------------------- |
| `User/app_run.c`        | `app_poll` / `app_init` / 当前时间缓存    |
| `User/main.c`           | 是否调用 `app_poll`；校时与限层               |
| `App/Link/pc_comm.c`    | `PCProcess`                         |
| `App/Pass/qr_comm.c`    | `QRProcessUart4/5`                  |
| `App/Cloud/g4g_comm.c`  | `G4GProcess`                        |
| `App/Link/app_comm.h`   | 通道函数声明汇总                            |
| `App/Link/rs485_comm.c` | `Rs485Process`（存在但当前未挂进 `app_poll`） |


