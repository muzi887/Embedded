# Keil 截图说明：Colors & Fonts 窗口列表

更新时间：2026-07-17

## 图片是什么地方

截图里高亮的是 `**Editor Text files**`，所在对话框为：

**Edit（编辑）→ Configuration…（配置）→ Colors & Fonts（颜色与字体）**

左侧列表用来选择「给哪一类窗口/编辑器改字体、颜色」，例如：


| 列表项                             | 作用                                 |
| ------------------------------- | ---------------------------------- |
| All Editors                     | 所有编辑器统一样式                          |
| C/C++ Editor files              | `.c` / `.h` 等源码编辑器                 |
| Asm Editor files                | 汇编编辑器                              |
| **Editor Text files**           | 普通文本（如 `.txt`、`.md` 等当文本打开时）的编辑器样式 |
| Build Output Window             | 编译输出窗口                             |
| Debug / Memory / UART #n Window | 调试与串口观察窗                           |


这是 **Keil 界面外观设置**，和工程目录、分组、编译无关。

---

## 不是「生成文件」的地方

若你要的是编译后生成 `.hex` / 输出目录，应去：

**Project → Options for Target… → Output**

- 勾选 **Create HEX File**（需要 hex 时）
- 可设输出目录（如 `..\OBJ\`）

若你要的是 **New Project / 加源码分组**，应去：

**Project → New µVision Project…**  
以及工程树里 **Add Group / Add Existing Files to Group**

详见：[Keil-New-Project只拷代码指南.md](./Keil-New-Project只拷代码指南.md)

---

## 小结


| 你想做的事      | 正确入口                                       |
| ---------- | ------------------------------------------ |
| 改编辑器字体颜色   | Edit → Configuration → Colors & Fonts（本截图） |
| 生成 HEX     | Options for Target → Output                |
| 新建工程、加组加文件 | Project / 工程树                              |


