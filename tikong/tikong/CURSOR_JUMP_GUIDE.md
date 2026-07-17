# Cursor 编辑器函数跳转功能使用指南

## 📋 目录
1. [快速跳转方法](#快速跳转方法)
2. [配置检查](#配置检查)
3. [故障排除](#故障排除)
4. [高级技巧](#高级技巧)

---

## 🚀 快速跳转方法

### 方法 1：快捷键跳转（最常用）
- **跳转到定义**：`F12`
- **跳转到声明**：`Ctrl+F12`
- **查看所有引用**：`Shift+F12`
- **返回上一位置**：`Alt+Left` 或 `Ctrl+-`
- **前进到下一位置**：`Alt+Right` 或 `Ctrl+Shift+-`

**使用步骤**：
1. 将光标放在函数名上（如 `delay_init`）
2. 按 `F12` 键
3. 自动跳转到函数实现

### 方法 2：鼠标点击跳转
- **Ctrl + 左键点击**：跳转到定义
- **Ctrl + Alt + 左键点击**：在新标签页打开定义
- **悬停显示信息**：鼠标悬停在函数名上，会显示函数签名和文档

**使用步骤**：
1. 按住 `Ctrl` 键
2. 将鼠标移到函数名上（函数名会变成可点击的链接）
3. 点击函数名跳转

### 方法 3：右键菜单
1. 右键点击函数名
2. 选择以下选项：
   - `Go to Definition` - 跳转到定义
   - `Go to Declaration` - 跳转到声明
   - `Go to Type Definition` - 跳转到类型定义
   - `Find All References` - 查找所有引用
   - `Peek Definition` - 预览定义（不跳转）

### 方法 4：命令面板
1. 将光标放在函数名上
2. 按 `Ctrl+Shift+P` 打开命令面板
3. 输入以下命令：
   - `Go to Definition` - 跳转到定义
   - `Go to Declaration` - 跳转到声明
   - `Peek Definition` - 预览定义
   - `Find All References` - 查找所有引用

### 方法 5：预览模式（Peek）
- **快捷键**：`Alt+F12`
- **功能**：在当前文件中预览函数定义，不跳转

**使用步骤**：
1. 将光标放在函数名上
2. 按 `Alt+F12`
3. 在当前文件下方弹出预览窗口
4. 按 `Esc` 关闭预览

---

## ⚙️ 配置检查

### 1. 确认 C/C++ 扩展已安装
- 打开扩展面板：`Ctrl+Shift+X`
- 搜索 `C/C++`（Microsoft 官方扩展）
- 确保已安装并启用

### 2. 检查配置文件
确保 `.vscode/c_cpp_properties.json` 文件存在且配置正确：

```json
{
    "configurations": [
        {
            "name": "STM32F407",
            "includePath": [
                "${workspaceFolder}/USER",
                "${workspaceFolder}/CORE",
                "${workspaceFolder}/FWLIB/inc",
                // ... 其他路径
            ],
            "defines": [
                "STM32F40_41xxx",
                "USE_STDPERIPH_DRIVER"
            ],
            "intelliSenseMode": "gcc-arm",
            "cStandard": "c99"
        }
    ],
    "version": 4
}
```

### 3. 检查状态栏配置
- 查看编辑器右下角状态栏
- 应该显示 `STM32F407`（你的配置名称）
- 如果显示其他配置，点击它并选择正确的配置

### 4. 验证 IntelliSense 工作状态
- 在代码中输入函数名的一部分（如 `delay_`）
- 按 `Ctrl+Space` 触发自动补全
- 如果能看到函数列表，说明 IntelliSense 正常工作

---

## 🔧 故障排除

### 问题 1：按 F12 没有反应

**解决方案**：
1. **重置 IntelliSense 数据库**
   - 按 `Ctrl+Shift+P`
   - 输入 `C/C++: Reset IntelliSense Database`
   - 回车执行
   - 等待索引完成（状态栏会显示进度）

2. **重新加载窗口**
   - 按 `Ctrl+Shift+P`
   - 输入 `Developer: Reload Window`
   - 回车执行

3. **检查文件关联**
   - 查看文件标签页右下角
   - 应该显示 `C` 或 `C++`
   - 如果不是，点击它并选择正确的语言

### 问题 2：跳转到错误的位置

**解决方案**：
1. **检查头文件包含**
   - 确保头文件使用正确的包含方式
   - 项目头文件使用 `"header.h"`
   - 系统头文件使用 `<header.h>`

2. **检查包含路径**
   - 打开 `.vscode/c_cpp_properties.json`
   - 确保所有必要的路径都在 `includePath` 中

3. **检查宏定义**
   - 确保必要的宏定义在 `defines` 中
   - 例如：`STM32F40_41xxx`、`USE_STDPERIPH_DRIVER`

### 问题 3：显示 "未找到定义"

**解决方案**：
1. **检查函数是否在项目中**
   - 使用 `Ctrl+Shift+F` 全局搜索函数名
   - 确认函数确实存在于项目中

2. **检查函数声明和定义是否匹配**
   - 函数签名必须完全一致
   - 检查参数类型和返回类型

3. **检查编译错误**
   - 查看 C/C++ 输出面板（`Ctrl+Shift+U`）
   - 选择 `C/C++` 输出
   - 查看是否有错误信息

### 问题 4：跳转很慢

**解决方案**：
1. **等待索引完成**
   - 首次打开项目需要建立索引
   - 状态栏右下角会显示索引进度
   - 等待索引完成后再使用跳转功能

2. **减少索引范围**
   - 在 `.vscode/c_cpp_properties.json` 中
   - 设置 `limitSymbolsToIncludedHeaders: true`
   - 只索引包含的头文件

---

## 💡 高级技巧

### 技巧 1：查看函数调用层次
- **快捷键**：`Ctrl+Shift+O`（在当前文件中查找符号）
- **命令**：`Go to Symbol in Workspace`（在整个工作区查找符号）

### 技巧 2：查看函数的所有引用
- **快捷键**：`Shift+F12`
- **功能**：显示函数在项目中的所有使用位置

### 技巧 3：使用面包屑导航
- **启用**：`View` → `Show Breadcrumbs`
- **功能**：在编辑器顶部显示当前位置的路径
- **跳转**：点击路径中的任意部分快速跳转

### 技巧 4：使用代码大纲
- **快捷键**：`Ctrl+Shift+O`（当前文件）
- **快捷键**：`Ctrl+T`（整个工作区）
- **功能**：快速查找和跳转到符号

### 技巧 5：多光标跳转
- 按住 `Alt` 键
- 点击多个函数名
- 然后按 `F12` 可以同时跳转到多个位置

### 技巧 6：使用标签页历史
- **返回**：`Ctrl+Tab` 切换最近使用的标签页
- **前进**：`Ctrl+Shift+Tab` 反向切换

---

## 📝 常用快捷键总结

| 功能 | 快捷键 | 说明 |
|------|--------|------|
| 跳转到定义 | `F12` | 跳转到函数/变量定义 |
| 跳转到声明 | `Ctrl+F12` | 跳转到函数/变量声明 |
| 查找所有引用 | `Shift+F12` | 查找符号的所有使用位置 |
| 预览定义 | `Alt+F12` | 在当前文件预览定义 |
| 返回上一位置 | `Alt+Left` | 返回跳转前的位置 |
| 前进到下一位置 | `Alt+Right` | 前进到跳转后的位置 |
| 查找符号 | `Ctrl+Shift+O` | 在当前文件查找符号 |
| 全局查找符号 | `Ctrl+T` | 在整个工作区查找符号 |
| 触发自动补全 | `Ctrl+Space` | 显示函数提示 |

---

## 🎯 实际使用示例

### 示例 1：跳转到 delay_init 函数
1. 打开 `USER/main.c`
2. 找到第14行：`delay_init(168);`
3. 将光标放在 `delay_init` 上
4. 按 `F12`
5. 跳转到 `SYSTEM/delay/delay.c` 第115行

### 示例 2：查看函数的所有调用
1. 将光标放在 `delay_init` 上
2. 按 `Shift+F12`
3. 查看所有调用 `delay_init` 的位置

### 示例 3：预览函数定义（不跳转）
1. 将光标放在 `delay_init` 上
2. 按 `Alt+F12`
3. 在当前文件下方预览函数定义
4. 按 `Esc` 关闭预览

---

## ✅ 验证跳转功能是否正常

### 快速测试步骤：
1. 打开 `USER/main.c`
2. 将光标放在 `delay_init`（第14行）
3. 按 `F12`
4. **预期结果**：跳转到 `SYSTEM/delay/delay.c` 第115行

如果跳转成功，说明配置正确！如果失败，请按照"故障排除"部分检查。

---

## 📚 相关资源

- [VS Code C/C++ 扩展文档](https://code.visualstudio.com/docs/languages/cpp)
- [IntelliSense 配置指南](https://code.visualstudio.com/docs/cpp/c-cpp-properties-schema-reference)

---

**最后更新**：2025-01-15
