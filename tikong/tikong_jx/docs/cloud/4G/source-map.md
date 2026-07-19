# 4G 源码对照地图（从 main 走）

更新时间：2026-07-18

> **何时读**：读完说明书 / AT 清单后仍对不上 `4G.c`、`usart3`、`uart3_at_sequence`、`G4GProcess` 时看本文。  
> **相关**：[pdf-reading-order.md](./pdf-reading-order.md)、[at-config-flow.md](./at-config-flow.md)（AT 配网整流程专文）

---

## 0. 先记住三句话

1. **一根串口**：模组 UART ↔ MCU **USART3**；收完一帧靠 DMA+IDLE，结果放在 `USART3_RX_BUF`，标志位 `USART3_RX_Complete`。
2. **主循环只有两条路**：`g_uart3_at_sequence_request != 0` → 配 AT；否则 → `app_poll()`（里面才有 `G4GProcess`）。**两路互斥，不会同时跑。**
3. **`4G.c` 不碰串口字节**：只管 RST / LINKA 这类 GPIO；AT 和 JSON 都走 USART3。

先不要按「五个文件从上到下通读」。按下面 **步骤 A → B → 选一路** 打开 Keil/编辑器跳转即可。

---

## 1. 总图（先看图再开文件）

```text
上电 board_init()
  ├─ uart3_init(115200)     → 串口硬件就绪（PD8/PD9）
  └─ GM4G_Init()            → RST/LINKA 脚就绪（与 AT 无关）

while(1)  ←── User/main.c
  │
  ├─【路① 配网】g_uart3_at_sequence_request == 1
  │     uart3_at_sequence_poll()
  │       发 AT → 等 USART3_RX_Complete → 看有没有 "OK"
  │       全部 OK → 写配置 / 复位模组 / 系统复位
  │     （此路不调用 app_poll，故没有 G4GProcess）
  │
  └─【路② 日常】request == 0
        app_poll()
          └─ G4GProcess()
                若 USART3_RX_Complete
                  → parseSerialData(USART3_RX_BUF)   // JSON 业务
```

共享缓冲：路①等的是 AT 回复（`OK`）；路②解析的是平台下发的 `数字,JSON`。  
**同一套** `USART3_RX_BUF` / `USART3_RX_Complete`，谁在主循环里用，取决于当前走哪一路。

---

## 2. 步骤 A：上电只干两件事（5 分钟）

打开 `User/board_init.c`，只认两行：

| 代码 | 作用 | 对应说明书 |
| --- | --- | --- |
| `uart3_init(115200); //4g` | 打开与模组通信的串口 | UART 通信口 |
| `GM4G_Init();` | 配 RST、读 LINKA | 复位脚、链路指示脚 |

再打开 `Hardware/Modem/4G.c`，只认：

- `GPIO_Pin_13` → 复位输出（`GM4G_Restart` 拉高再拉低）
- `GPIO_Pin_14` → LINKA 输入 + 中断 → 亮灯

**验收**：能说出「串口归 usart3，脚归 4G.c」。对不上 AT 是正常的——这一步本来就没有 AT。

---

## 3. 步骤 B：收包公共底座（10 分钟）

打开 `Hardware/Serial/usart3.c`，只抓三个名字：

| 符号 | 含义 |
| --- | --- |
| `USART3_RX_BUF` | 收到的字节堆这里 |
| `USART3_RX_CNT` | 本帧长度 |
| `USART3_RX_Complete` | IDLE 定帧完成 = 1，主循环可取用 |

发送函数：``（AT 序列发指令、业务回执都会用到）。

读头 UART4 的 DMA+IDLE 思路相同，可对照 [uart4-dma-rx.md](../../serial/uart4-dma-rx.md)；这里不必深挖 DMA 寄存器。

**验收**：模组任意回一串字符 → 最终都会进 `USART3_RX_BUF` 并置 `USART3_RX_Complete`。

---

## 4. 步骤 C：只走「路① 配网」（20 分钟）

目的：把 `AT.txt` 和代码对上，**先不要打开** `Live_data_r.c`。

1. 打开 `User/main.c` 约 33～51 行：`if (g_uart3_at_sequence_request)` 分支。  
2. 打开 `App/Cloud/uart3_at_sequence.c`：  
   - `uart3_at_build_cmd_table()`（约 120 行起）：组出与 `AT.txt` 同系列的 `AT+WKMOD`…`AT+S`  
   - `uart3_at_sequence_poll()`：`SEND` → 等 `USART3_RX_Complete` → 缓冲里找 `OK` → 下一条  
3. 谁置位 request：`App/Pass/cmd.c` 里设置类命令会把 `g_uart3_at_sequence_request = 1`（配 MQTT 参数后）。

对照表：

| 你手里的资料 | 代码里看什么 |
| --- | --- |
| `AT.txt` 第 N 条 | `s_cmd_bufs` / `BC(n, ...)` |
| `MQTT_AT_SET.txt` 的 `OK` | `poll` 里 `strstr(..., "OK")` |
| 配网时日志 `[AT_SEQ]` | 同一文件里的 `printf` |

**验收**：能指着 `main` 说「request=1 时只跑 AT，不跑 G4GProcess」。

---

## 5. 步骤 D：只走「路② 日常 JSON」（20 分钟）

前提：request 已为 0（没在配网）。

1. `User/app_run.c` → `app_poll()`：最后一行 `G4GProcess();`  
2. `App/Cloud/g4g_comm.c` 全文很短：  
   - 若 `USART3_RX_Complete` → 打印 → `parseSerialData((const char *)USART3_RX_BUF)` → 清标志  
3. `App/Cloud/Live_data_r.c` → `parseSerialData`：  
   - 格式：`数字,JSON`（先找逗号）  
   - `switch (caseNumber)`：如 `101` RS485 透传、`104` 黑名单等  
   - **追 case 101 专文**：[parseSerialData-case101.md](../parseSerialData-case101.md)  

阶段 D 建议：在 `switch` 里 **只追一个 case**（例如 101），看到本地动作 + 回执函数即可，不要把整个 `Live_data_r.c` 读完。

**验收**：能画「USART3 收完 → G4GProcess → parseSerialData → 某一个 case」。

---

## 6. 为什么原来「五个文件列表」难对照

| 列表里的项 | 容易误解 | 实际关系 |
| --- | --- | --- |
| `4G.c` | 以为这里发 AT | 只 GPIO；AT 在 `uart3_at_sequence` |
| `usart3.c` | 以为是业务 | 只是收发管道 + 缓冲 |
| `uart3_at_sequence` | 和 G4G 混在一起 | **路①**，与路②互斥 |
| `G4GProcess` | 以为一直在跑 | 只在 `app_poll` 里，路①时不进 |
| `parseSerialData` | 以为收任何串口数据都进 | 只处理路②下的业务帧（`数字,JSON`） |

对照口诀：**先分路，再开文件；同一缓冲，两种读法。**

---

## 7. 最小阅读清单（可打勾）

- [ ] `main.c`：找到 `g_uart3_at_sequence_request` 的 if/else  
- [ ] `4G.c`：RST=PB13，LINKA=PB14（不读 AT）  
- [ ] `usart3.c`：知道 `USART3_RX_Complete` 含义  
- [ ] `uart3_at_sequence.c`：`BC(...)` 与 `AT.txt` 对上至少 3 条  
- [ ] `g4g_comm.c`：全文扫一遍（几十行）  
- [ ] `parseSerialData`：只跟一个 `case`

做完即达到 [pdf-reading-order.md](./pdf-reading-order.md) 阶段 D「源码对照」目标；不必一次读透五个文件。
