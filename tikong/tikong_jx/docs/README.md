# docs/ — 梯控固件文档索引（tikong_jx）

更新时间：2026-07-17

本目录存放 **`tikong/tikong_jx`** 梯控固件说明（江协风格工程）。  
源码在工程根目录；Keil 工程为 `User/tikong.uvprojx`。  
分层约定见仓库根 `.cursorrules`（`User` / `App` / `Hardware` / `System` / `Middlewares` / `Library` / `Start`）。

**约束：本仓库日常改动完全不进行任何 git 操作（除非用户当次明确要求）。**

---

## 目录树

```text
docs/
├── README.md          ← 本索引
├── overview/          跨模块：总览、入门、清单、主流程图
├── pass/              通行（对齐 App/Pass；专文待补）
├── cloud/             云（对齐 App/Cloud；含 4g/ 资料导读）
├── store/             存储 / EEPROM
├── serial/            串口通路
├── setup/             工程搭建、Keil、目录重构说明
├── superpowers/       AI 规格与实施计划
```

---

## overview/ — 跨模块

| 文件 | 说明 |
| --- | --- |
| [project-overview.md](./overview/project-overview.md) | 工程总览：定位、架构、串口、能力 |
| [onboarding-f103.md](./overview/onboarding-f103.md) | 对照 F103 基础的接手规划 |
| [source-inventory.md](./overview/source-inventory.md) | 源码职责清单（填表） |
| [inventory-template.md](./overview/inventory-template.md) | 清单方法与空模板 |
| [main-flowchart.mmd](./overview/main-flowchart.mmd) | 主程序流程图（Mermaid，部分节点偏旧） |
| [flows/app-poll-flow.md](./overview/flows/app-poll-flow.md) | `app_poll()`：日常四路轮询与 AT 互斥 |
| [flows/directory-tree.md](./overview/flows/directory-tree.md) | 工程目录树（按文件作用） |
| [naming-conventions.md](./overview/naming-conventions.md) | 命名规则与有问题命名清单 |
| [embedded-coding-style.md](./overview/embedded-coding-style.md) | 通用嵌入式编程风格（裸机/分层/中断/内存）；文末对照本工程 |
| [watchdog-iwdg.md](./overview/watchdog-iwdg.md) | 本工程独立看门狗 IWDG：超时、喂狗点、调试注意 |
| [interrupt-service.md](./overview/interrupt-service.md) | 中断服务：短 ISR 原则与本工程各向量职责 |
| [optimization-directions.md](./overview/optimization-directions.md) | 优化方向：逻辑清晰度与过长文件拆分 |
| [pass-split-migrate-list.md](./overview/pass-split-migrate-list.md) | Pass 拆分：迁出了什么 → 变成了什么 |
| `开发主要问题.doc` | 开发问题备忘（二进制） |
| `梯控门禁网关V2_20260429(2).docx` | 产品/网关说明（二进制） |

---

## pass/ — 通行

| 文件 | 说明 |
| --- | --- |
| [qr-process-uart45.md](./pass/qr-process-uart45.md) | `QRProcessUart4` / `QRProcessUart5`：读头轮询、JSON 切片、进 `CommControl` |
| [memcpy-memset.md](./pass/memcpy-memset.md) | `memcpy` / `memset`：标准库用法与读头路径中的作用 |
| [commcontrol-permission-chain.md](./pass/commcontrol-permission-chain.md) | `CommControl` 调用条件；与 `Cmd_Permission` → `ProcessElevator` 关系 |
| [password-4digit-auth.md](./pass/password-4digit-auth.md) | 四位口令固件校验：概念、时间槽、摘要、拆位与授权 |
| [password-4digit-collision.md](./pass/password-4digit-collision.md) | 四位口令重复/碰撞：平台源串与模运算空间局限 |
| [password-4digit-unacceptable.md](./pass/password-4digit-unacceptable.md) | 梯控场景下不可接受的口令重复（越权呼层、跨设备等） |
| [password-4digit-remediation.md](./pass/password-4digit-remediation.md) | 针对不可接受问题的解决思路（配置优先 / 改算法） |
| [password-4digit-design-prompt.md](./pass/password-4digit-design-prompt.md) | 四位访客密码加密方案：发给 AI 的优化设计 Prompt |
| [sha1-hash.md](./pass/sha1-hash.md) | `sha1_hash`：API、SHA-1 步骤与 `Middlewares/sha1` 对照 |

其它源码见 `App/Pass/`（`qr_comm`、`cmd`、`data_handler`）。

---

## cloud/ — 云

| 文件 | 说明 |
| --- | --- |
| [parseSerialData-case101.md](./cloud/parseSerialData-case101.md) | `parseSerialData`：`数字,JSON`；以 case 101 RS485 透传追本地动作+回执 |
| [thing-model-v2.md](./cloud/thing-model-v2.md) | 网关 V2 物模型：事件/服务表格 + 固件对照 |
| [thing-model-v2.json](./cloud/thing-model-v2.json) | 网关 V2 物模型完整 JSON |
| [4g/pdf-reading-order.md](./cloud/4g/pdf-reading-order.md) | 阶段 D：本目录 PDF/资料阅读顺序与源码对照 |
| [4g/source-map.md](./cloud/4g/source-map.md) | 阶段 D：从 `main` 分叉对照 4G 源码（配网路 / JSON 路） |
| [4g/at-config-flow.md](./cloud/4g/at-config-flow.md) | 4G AT 配网整流程：触发、状态机、19 条命令、EEPROM/复位 |
| [4g/foolproof-header.md](./cloud/4g/foolproof-header.md) | 说明书「双排插针防呆设计」含义（装配防错，非 AT） |

源码见 `App/Cloud/`（`g4g_comm`、`Live_data*`、`uart3_at_sequence`）；模组驱动见 `Hardware/Modem/`。

---

## store/ — 存储

| 文件 | 说明 |
| --- | --- |
| [storage-logic.md](./superpowers/storage-logic.md) | 阶段 E：上电装载、黑名单/配置/RTC/限层回写逻辑 |
| [blacklist-logic.md](./store/blacklist-logic.md) | 黑名单：RAM/EEPROM、通行检查、云端 case、读头死路径与待做 |
| [eeprom.md](./store/eeprom.md) | 外挂 EEPROM：AT24C32 地址布局、驱动 API |

---

## serial/ — 串口硬件通路

| 文件 | 说明 |
| --- | --- |
| [uart4-dma-rx.md](./serial/uart4-dma-rx.md) | UART4：DMA + IDLE 定帧 → `QRProcessUart4` |

---

## setup/ — 工程搭建

| 文件 | 说明 |
| --- | --- |
| [rebuild-jiangxie.md](./setup/rebuild-jiangxie.md) | 旁路新建：拷贝对照总表 |
| [keil-new-project.md](./setup/keil-new-project.md) | Keil New Project 只拷代码 |
| [keil-colors-fonts.md](./setup/keil-colors-fonts.md) | Colors & Fonts 说明（非工程生成） |
| [dir-refactor-plan.md](./setup/dir-refactor-plan.md) | 目录问题与江协重构方案总览 |

---

## superpowers/

| 文件 | 说明 |
| --- | --- |
| [specs/2026-07-17-江协风格目录重构-design.md](./superpowers/specs/2026-07-17-江协风格目录重构-design.md) | 源码目录重构设计 |
| [plans/2026-07-17-江协风格目录重构.md](./superpowers/plans/2026-07-17-江协风格目录重构.md) | 源码重构分步清单 |
| [specs/2026-07-17-docs-主题域整理-design.md](./superpowers/specs/2026-07-17-docs-主题域整理-design.md) | 本文档主题域整理设计 |
| [plans/2026-07-17-docs-主题域整理.md](./superpowers/plans/2026-07-17-docs-主题域整理.md) | 本文档整理实施计划 |

---

## 旧文件名 → 新路径

| 旧名 | 新路径 |
| --- | --- |
| `文件说明.md` | `README.md`（本文件） |
| `梯控固件项目介绍.md` | `overview/project-overview.md` |
| `梯控固件入门规划-基于F103基础.md` | `overview/onboarding-f103.md` |
| `源码文件清单.md` | `overview/source-inventory.md` |
| `源码文件清单-方法与模板.md` | `overview/inventory-template.md` |
| `main_flowchart.mmd` | `overview/main-flowchart.mmd` |
| `EEPROM说明.md` | `store/eeprom.md` |
| `UART4-DMA收包路径说明.md` | `serial/uart4-dma-rx.md` |
| `从零重建江协风格工程指南.md` | `setup/rebuild-jiangxie.md` |
| `Keil-New-Project只拷代码指南.md` | `setup/keil-new-project.md` |
| `Keil截图-Colors与Fonts说明.md` | `setup/keil-colors-fonts.md` |
| `目录问题与重构方案.md` | `setup/dir-refactor-plan.md` |

---

## 架构速记（tikong_jx）

| 层级 | 目录 |
| --- | --- |
| 入口 / Keil | `User/` |
| 通行 / 云 / 存储业务 / 通道 | `App/Pass`、`App/Cloud`、`App/Store`、`App/Link` |
| 串口 / 存储芯片 / 时间 / 板级 / 4G | `Hardware/Serial`、`Storage`、`Time`、`Board`、`Modem` |
| 延时 / sys | `System/` |
| cJSON / SHA1 | `Middlewares/` |
| SPL / 启动与 CMSIS | `Library/`、`Start/` |
