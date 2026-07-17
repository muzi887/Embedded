# Microsoft C/C++：`compile_commands.json` 与 F12

本仓库用 **VS Code / Cursor + `ms-vscode.cpptools`** 做 IntelliSense；**Clangd** 不参与工程配置。

- **数据源**：[compile_commands.json](compile_commands.json)（`generate_compile_commands.ps1`）+ [`.vscode/c_cpp_properties.json`](.vscode/c_cpp_properties.json)。
- **`Win32` 配置**：在扩展里这是 **Windows 专用名**，会自动选中。[`compile_commands.json`](compile_commands.json) 挂在这段配置上；不要用错成 **Template_Template**。
- **`Win32` 不是 x86 MSVC**：这里仅借用名字满足扩展的自动选型，实际 IntelliSense 仍走 **clang-arm + ARM 编译参数**（来自 `compile_commands`）。

### 为何会「修了还是 F12 失败」（已在本仓库对症下药）

| 原因 | 本仓库处理方式 |
|------|----------------|
| **用户全局 `compilerPath` 或未显式清空** 干扰 `compile_commands`（[vscode-cpptools#11889](https://github.com/microsoft/vscode-cpptools/issues/11889)） | 工作区 [.vscode/settings.json](.vscode/settings.json)：`"C_Cpp.default.compilerPath": ""`；Win32 **`c_cpp_properties` 不写 `compilerPath`**，完全交给 `compile_commands` |
| 活动配置不是挂 `compileCommands` 的那一个 | Windows 上使用名为 **`Win32`** 的配置以自动对齐 |
| `compile_commands` 单行 `command` 在 Win 上含引号，解析翻车 | `generate_compile_commands.ps1` 改用 **`arguments` 数组** |
| **Browse（Tag Parser）没有扫全仓库 `.c`**，跨文件只能落到 `.h` 声明 | **`browse.path`**: **`${workspaceFolder}/**`**，`**limitSymbolsToIncludedHeaders`: false（`Win32` / `Template_Template` 与 **`C_Cpp.default.browse.*`**）；改完后 **Reset IntelliSense Database** 并等右下角 C/C++ 扫库结束；可再试 **Ctrl+F12（转到实现）** |

### F12 只到头文件、「实现」缺位时

跨文件跳转除当前文件的 IntelliSense 外，还会用 **Browse 数据库**在项目里定位 **`.c` 符号**。Browse 不完整时，`void HC595_ShiftByte(...)` 这类 API 常会只跳进 **`.h` 前置声明**。这与 [vscode-cpptools #2053](https://github.com/microsoft/vscode-cpptools/issues/2053)、[#13697](https://github.com/microsoft/vscode-cpptools/issues/13697) 中描述的行为一致。

## F12 异常时请按顺序做

1. 仓库根：**`fix_code_index.cmd`** 或 **`generate_compile_commands.ps1`**（会刷新 **`compile_commands.json`** 并把 **`Win32`** 段落合并进 `c_cpp_properties`；**不改**你已提交的 [.vscode/settings.json](.vscode/settings.json)）。
2. 看 VS Code **状态栏**：应为 **C/C++: Win32**（若点了别的名字，换回 **Win32**）。
3. `Ctrl+Shift+P` → **C/C++: Reset IntelliSense Database**。
4. 仍不行：**Developer: Reload Window**。

## 自检

| 现象 | 处理 |
|------|------|
| 只能进 `.h`（不到 `.c` 实现） | 状态栏选 **Win32**；确认已 **Reset IntelliSense Database**（Browse 已改为递归 **`${workspaceFolder}/**`**）；试 **Ctrl+F12**；仍弱则核对 **LLVM/clang** 是否可用，`compile_commands` 是否仍为 **arguments** + clang 条目 |
| 部分 `.c` 无条目 | `compile_commands` 只为仓库内扫描到的 **`*.c`** 生成 |
| IntelliSense 持续报错 | 输出面板切换到 **C/C++**，看是否与 **Keil 头文件路径/D 盘 Keil 安装路径** 不一致 |

### 磁盘缓存

**`.vscode\BROWSE.VC.DB*`**：**fix_code_index.cmd** 会尝试删掉；占用则先关编辑器再跑一次。
