# 当前工程优化方向

更新时间：2026-07-20  
定位：说明 **tikong_jx 下一阶段应往哪改**，不是逐步实施勾选清单的替代。  
已完成：目录江协化；**Pass 域超长文件主体拆分**（见 [pass-split-migrate-list.md](./pass-split-migrate-list.md)）。  
下文：目录拆完、Pass 拆完之后，**还剩什么清晰度 / 产品债**。

相关文档：[naming-conventions.md](./naming-conventions.md)、[interrupt-service.md](./interrupt-service.md)、[project-overview.md](./project-overview.md)、[pass-split-migrate-list.md](./pass-split-migrate-list.md)

---

## 1. 现状判断

1. **Pass 主路径已可按文件认职责**：口令 / 权限 / 设置 / 验签分文件；`qr_comm` 与 `cmd` 变薄。  
2. **Cloud 与部分杂项仍偏厚**：`Live_data_r.c`、`data_handler` 组帧/黑名单包等仍是下一刀。  
3. **产品逻辑债仍在**：四位口令算法替换、命名拼写等与结构拆分正交。

---

## 2. 过长文件（业务侧，约 2026-07-20）

统计范围：工程内自有源码（不含 `Library/`、`Start/`、`Middlewares/`）。


| 行数（约） | 文件 | 说明 |
| ----- | ------------------------- | --- |
| ~824 | `App/Cloud/Live_data_r.c` | **下一切割重点**：下行物模型多 case |
| ~700 | `App/Pass/data_handler.c` | 已瘦掉 crypto；余凭证 / 黑名单包 / 组帧 |
| ~548 | `App/Pass/pass_setting.c` | 新文件，可接受；CSV 辅助可再抽 |
| ~529 | `App/Pass/pass_permission.c` | 新文件，可接受 |
| ~412 | `App/Pass/qr_comm.c` | 已去口令/时间工具；余双 UART 组帧复制 |
| ~374 | `Hardware/Board/timer.c` | 多定时器 ISR |
| ~346 | `App/Cloud/Live_data.c` | 多种 `*_Reply` |
| ~249 | `App/Pass/pass_crypto.c` | 已拆出 |
| ~231 | `App/Pass/pass_password.c` | 已拆出 |
| ~161 | `App/Pass/cmd.c` | **已瘦成门面**（原 ~1000+） |


经验阈值（本工程建议）：


| 规模 | 建议 |
| ----------- | ---------- |
| < 300 行 | 可接受 |
| 300～500 行 | 关注是否仍单一职责 |
| **> 500 行** | 优先拆分候选 |
| **> 800 行** | 强烈建议拆 |


---

## 3. 「逻辑不够清楚」具体指什么

不止「行数多」，还包括：


| 现象 | 例 |
| ----------------------- | ----------------------------------------------------------------------- |
| 一条业务路径跨太多函数/文件，中间靠全局量传递 | `g_device_*`、`g_result`、各口 `RX_Complete` |
| 同概念多名、拼写不一 | 见 [naming-conventions.md](./naming-conventions.md) |
| 设备种类分支重复 | 园门/单元/电梯在 permission 与 password 中仍各有一份策略 |
| 驱动与业务边界偶发模糊 | 定时器里既有节拍又有业务标志语义 |
| 文档与实现两套故事 | 旧四位口令问题已有专文，新方案在 `docs/pass/newpw/`，固件仍为旧逻辑 |
| 中断与主循环职责需持续守住 | 见 [interrupt-service.md](./interrupt-service.md) |


优化目标：**「读头一帧 → 判定 → 动作 → 上报」短文件链**（Pass 侧已基本具备）。

---

## 4. 优化方向（按优先级）

### P0 — Pass 超长文件拆分

**状态：主体已完成。** 细节与函数对照见 [pass-split-migrate-list.md](./pass-split-migrate-list.md)。

当前落盘：

```text
App/Pass/
  qr_comm.c / .h           ← QRProcessUart4/5（组帧 + type 分发）
  pass_password.c / .h     ← qr_handle_password_input …
  cmd.c / .h               ← CommControl 薄门面 + 黑名单残留
  pass_permission.c / .h   ← Cmd_Permission 链
  pass_setting.c / .h      ← Setting / 校时 / 限层
  pass_crypto.c / .h       ← 验签 / 时间窗
  data_handler.c / .h      ← g_device_*、凭证、组帧杂项

Hardware/Time/rtc_util.*   ← ds1302_shift_minutes、create_ymdhm
```

调用关系：

```text
app_poll
  → QRProcessUart4/5
       → type '2'  → qr_handle_password_input
       → 其它      → CommControl → pass_setting / pass_permission
```

**Pass 侧剩余（小）：**

- UART4/5 `find_sub` / 组帧双份合并（可选）  
- `data_handler.h` 与 `pass_crypto.h` 声明去重  
- 黑名单命令是否迁 `App/Store`（正交，非阻塞）

原则（仍适用后续 Cloud 拆分）：

- **一个 `.c` 一个主故事**；对外入口宜少。  
- 跨文件 API **非 static**；仅本文件帮手用 `static`，且勿写入头文件。  
- 先结构、后算法；Keil 须加入新 `.c`。

### P0b / 下一刀 — Cloud 域

```text
Live_data_r.c  → 按服务/属性拆 case 到多个 parse_*.c，或「一张表 + 小函数」
Live_data.c    → 上报按主题拆 reply_*.c（可渐进）
```

### P1 — 理清控制流与数据流

- 为 Pass / Cloud 各补一页「主路径」文档（可放 `docs/pass/`、`docs/cloud/`）。  
- 减少隐式全局；`g_result` 已统一在 `Live_data.c` 定义。  
- 新逻辑优先参数传入。

### P2 — 命名与可读性（改到再动）

遵循 [naming-conventions.md](./naming-conventions.md)：触碰时修正拼写；禁止新增拼音标识符。

### P3 — 产品逻辑债

- 四位口令：旧方案见 `docs/pass/password-4digit-*.md`；新剖面见 `docs/pass/newpw/`。  
- 口令已在 `pass_password.c`，替换算法时不必掀翻 `qr_comm`。

### P4 — 定时器与中断文件瘦身（次要）

`timer.c` 可按 TIM 职责拆文件；遵守 [interrupt-service.md](./interrupt-service.md)。

### 明确非目标（近期）

- 不为风格引入 RTOS。  
- 不移动 `Library/` / `Middlewares/` / 厂商头。  
- 不做无行为差异的全库格式化/改名。  
- 目录已江协化，**不再**把「再搬一层目录」当主优化。

---

## 5. 拆分时的质量门槛

每拆出一块，至少满足：

1. **可编译、可下载**：Keil 工程已加入新文件。  
2. **行为不变**：先结构重构，再改算法。  
3. **有一条手工验收路径**：口令 / 权限二维码 / 云下行任选。  
4. **文件目标行数**：新建业务 `.c` 尽量 < 400～500 行；老文件应明显下降。  
5. **文档**：入口或调用链变了则更新 `docs/pass` / `docs/cloud` 与对照表。

---

## 6. 建议推进顺序（给人排期用）

```text
①~~ Pass 主路径图 / 口令抽离 ~~     ← 已完成
②~~ 拆 cmd 权限 vs 设置 ~~         ← 已完成
③~~ 瘦 data_handler（crypto）~~    ← 已完成
④ Pass 收尾：find_sub 合并、头文件去重（可选）
⑤ Live_data_r 按 case 拆分          ← 当前主战场
⑥ 顺手命名修正 + 四位口令方案替换（单独里程碑）
```

每步可单独合入，避免「大爆炸重构」。

---

## 7. 成功长什么样

- 打开 `App/Pass/`，能根据文件名猜出职责（**已基本达到**）。  
- 「读头口令」「二维码权限」「设备设置」各自文件链短（**已基本达到**）。  
- Cloud 侧同样可指认修改落点；单业务 `.c` 普遍 < 500～800 行。  
- 新同事按 [onboarding](./onboarding-f103.md) + 本文 + [pass-split-migrate-list](./pass-split-migrate-list.md)，能在一天内指认 Pass 落点。

---

## 8. 文档关系


| 文档 | 关系 |
| ----------------------------------------------------- | ------------------- |
| 本文 | **优化方向与优先级** |
| [pass-split-migrate-list.md](./pass-split-migrate-list.md) | Pass 拆分：迁出对照与进度（P0 已落地） |
| [dir-refactor-plan.md](../setup/dir-refactor-plan.md) | 已完成的目录层重构（背景） |
| [naming-conventions.md](./naming-conventions.md) | 命名债与新增规则 |
| [embedded-coding-style.md](./embedded-coding-style.md) | 通用嵌入式写法 |
| [interrupt-service.md](./interrupt-service.md) | 中断边界 |
| `docs/pass/newpw/`* | 口令算法演进（配合 P3） |
