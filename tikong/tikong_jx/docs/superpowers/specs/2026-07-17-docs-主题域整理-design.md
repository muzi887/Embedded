# docs/ 主题域整理 — 设计说明

日期：2026-07-17  
状态：**已批准；已实施（2026-07-17）**  
工程：`tikong/tikong_jx`  
范围：仅重组 `docs/` 内文档；不改固件源码、不改 Keil 工程。  
**硬性约束：完全不进行任何 git 操作。**

---

## 1. 目标与非目标

### 目标

- 按**主题域**整理文档，目录名与源码分层对齐：`pass` / `cloud` / `store` / `serial` / `setup`。
- 根目录只保留 **`README.md` 索引**；跨模块文档进 `overview/`。
- 允许**重命名**文件（英文短名）；正文中明显过期的旧路径改为 `tikong_jx` 现行路径。
- 保留既有 `superpowers/` 结构（`specs/`、`plans/`）。

### 非目标

- 不新写业务专文内容（`pass/`、`cloud/` 可先空壳 + 索引说明）。
- 不从旧工程 `tikong/tikong` 搬运缺失的根目录 md / USER 流程图（除非本仓库已有）。
- **完全不进行任何 git 操作**（含 status / commit / push 等）。

---

## 2. 已确认选择

| 项 | 选择 |
| --- | --- |
| 分类主轴 | 主题域（Pass / Cloud / 存储 / 串口硬件 / 工程搭建） |
| 跨模块文档 | 根目录仅 `README.md`；其余进 `overview/`；保留 `superpowers/` |
| 目录命名 | 方案 1：英文短目录，一层深度 |
| 文件名 | 可改为英文短名；文档标题可保留中文 |

---

## 3. 目标目录树

```text
docs/
├── README.md                      # 唯一根索引（由 文件说明.md 改写）
├── overview/                      # 跨模块
│   ├── project-overview.md        # ← 梯控固件项目介绍.md
│   ├── onboarding-f103.md         # ← 梯控固件入门规划-基于F103基础.md
│   ├── source-inventory.md        # ← 源码文件清单.md
│   ├── inventory-template.md      # ← 源码文件清单-方法与模板.md
│   └── main-flowchart.mmd         # ← main_flowchart.mmd
├── pass/                          # 通行（对齐 App/Pass）
│   └── .gitkeep                   # 暂无专文；README 注明见源码
├── cloud/                         # 云（对齐 App/Cloud）
│   └── .gitkeep
├── store/                         # 存储业务 / EEPROM（对齐 App/Store + Hardware/Storage）
│   └── eeprom.md                  # ← EEPROM说明.md
├── serial/                        # 串口通路（对齐 Hardware/Serial）
│   └── uart4-dma-rx.md            # ← UART4-DMA收包路径说明.md
├── setup/                         # 工程搭建 / 重建 / Keil
│   ├── rebuild-jiangxie.md        # ← 从零重建江协风格工程指南.md
│   ├── keil-new-project.md        # ← Keil-New-Project只拷代码指南.md
│   ├── keil-colors-fonts.md       # ← Keil截图-Colors与Fonts说明.md
│   └── dir-refactor-plan.md       # ← 目录问题与重构方案.md
└── superpowers/                   # 结构不变
    ├── specs/
    │   ├── 2026-07-17-江协风格目录重构-design.md
    │   └── 2026-07-17-docs-主题域整理-design.md   # 本文件
    └── plans/
        └── 2026-07-17-江协风格目录重构.md
```

---

## 4. 搬家与重命名对照表

| 现状（docs/ 根或既有路径） | 目标 |
| --- | --- |
| `文件说明.md` | 删除；内容改写为 `README.md` |
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
| `superpowers/**` | 路径不变 |

---

## 5. 内容修订约定（搬家时一并做）

1. **`README.md`**：按新树重写索引表；工程路径写 `tikong_jx`；Keil 工程写 `User/tikong.uvprojx`；分层用现行 `User` / `App` / `Hardware` 等；去掉本仓库不存在的根目录文档链接（如旧 `PROJECT_STRUCTURE.md`），或标注「仅旧工程」。
2. **专文内相对链接**：凡指向已搬家文件的链接，改为新相对路径。
3. **明显过期路径**：文中 `USER/`、`app/comm/`、`drivers/`、`FWLIB/` 等旧路径，在触及到的文件里改为现行路径（不强行全文考古无关段落）。
4. **`pass/`、`cloud/`**：创建 `.gitkeep`；在 `README.md` 写明「专文待补，见 `App/Pass`、`App/Cloud`」。
5. **`.cursorrules`**：若提及 `docs/` 布局，可补一句主题域索引在 `docs/README.md`（可选，实施计划里决定是否做）。

---

## 6. 验收标准

- [x] `docs/` 根目录仅有 `README.md` 与子目录（无散落业务 md / mmd）。
- [x] 对照表中每个源文件都在目标路径，旧文件名不再残留于根目录。
- [x] `README.md` 链接均可点开（相对路径正确）。
- [x] `superpowers/` 未被挪动或打散。
- [x] 空主题目录 `pass/`、`cloud/` 存在（含 `.gitkeep`）。

---

## 7. 风险与回退

| 风险 | 缓解 |
| --- | --- |
| 外部书签 / 对话仍引用旧中文文件名 | `README.md` 保留「旧名 → 新路径」对照一行表 |
| 文内交叉链接漏改 | 实施时对 `docs/**/*.md` 做旧文件名 grep |
| 空目录被 git 忽略 | 使用 `.gitkeep` |

回退：用 git 还原 `docs/` 即可（实施前若无 commit，保留一次目录副本或依赖工作区历史）。
