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
├── cloud/             云（对齐 App/Cloud；专文待补）
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
| [main-flowchart.mmd](./overview/main-flowchart.mmd) | 主程序流程图（Mermaid） |
| `开发主要问题.doc` | 开发问题备忘（二进制） |
| `梯控门禁网关V2_20260429(2).docx` | 产品/网关说明（二进制） |

---

## pass/ — 通行

暂无独立专文。源码见 `App/Pass/`（`qr_comm`、`cmd`、`data_handler`）。

---

## cloud/ — 云

暂无独立专文。源码见 `App/Cloud/`（`g4g_comm`、`Live_data*`、`uart3_at_sequence`）。

---

## store/ — 存储

| 文件 | 说明 |
| --- | --- |
| [eeprom.md](./store/eeprom.md) | 外挂 EEPROM：地址布局、黑名单/公钥、驱动 API |

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
