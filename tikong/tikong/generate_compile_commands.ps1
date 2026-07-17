Param(
    [string]$OutputFile = "compile_commands.json",
    [string]$KeilArmccPath = "D:/Keil5/ARM/ARMCC/bin/armcc.exe",
    [string]$IdeCompiler = ""
)

# Keil 实际编译仍用 armcc/armclang；以下为 VS Code/Cursor 生成 compile_commands 供 Microsoft C/C++ 使用。
# 数据库里宜用与 IntelliSense（clang-arm）相容的 Clang 兼容驱动 + 选项；纯 armcc 命令行 MSVC 后端难以解析。

$workspace = (Get-Location).Path

$commonIncludeArgs = @(
    '-I.', '-ICORE', '-ISYSTEM/delay', '-ISYSTEM/sys', '-ISYSTEM/usart',
    '-IUSER', '-IFWLIB/inc', '-Ithird_party', '-Idrivers', '-Iboard', '-Iapp',
    '-Iapp/utils', '-Iapp/comm', '-IFWLIB/src',
    '-ID:/Keil5/ARM/ARMCC/include', '-ID:/Keil5/ARM/ARMCC/include/rw'
)
$armccArgList = @(
    '-c', '-O0', '--c99',
    '-DSTM32F40_41xxx', '-DUSE_STDPERIPH_DRIVER', '-D__CC_ARM', '-D__arm__'
) + $commonIncludeArgs
# 刻意不加 -D__CC_ARM，让 CMSIS 走 __GNUC__/__clang__ 分支，Clang 才能正确解析内联与跨 TU 索引
# -fms-extensions：ARMCC 头文件中大量使用 __declspec，需打开 MSVC 风格扩展才可被 IntelliSense 稳定解析。
$clangArgList = @(
    '-std=c99', '-xc', '-c', '-O0',
    '--target=thumbv7em-none-eabi',
    '-mcpu=cortex-m4', '-mfpu=fpv4-sp-d16', '-mfloat-abi=hard',
    '-Wno-unused-command-line-argument', '-Wno-unknown-warning-option',
    '-fms-extensions',
    '-DSTM32F40_41xxx', '-DUSE_STDPERIPH_DRIVER', '-D__arm__'
) + $commonIncludeArgs

function Test-CompilerPath {
    param([string]$Path)
    return ($Path -and (Test-Path -LiteralPath $Path))
}

function Get-ClangLikeCompiler {
    param(
        [string]$Explicit,
        [string]$Ws
    )
    $candidates = @()
    if ($Explicit) { $candidates += $Explicit }
    if ($env:TIKONG_CLANG_EXE) { $candidates += $env:TIKONG_CLANG_EXE.Trim() }
    # 优先 LLVM/host clang：`compile_commands` 用作 IntelliSense「翻译单元命令行」时与 clang-arm 模式一致。
    $pf = $env:ProgramFiles
    $pf86 = ${env:ProgramFiles(x86)}
    $candidates += @((Join-Path $Ws "tools\clang.exe"))
    if ($pf) {
        $candidates += (Join-Path $pf "LLVM\bin\clang.exe")
    }
    if ($pf86) {
        $candidates += (Join-Path $pf86 "LLVM\bin\clang.exe")
    }
    $candidates += @(
        "D:/Keil5/ARM/ARMCLANG/bin/armclang.exe",
        "C:/Keil5/ARM/ARMCLANG/bin/armclang.exe"
    )
    foreach ($p in $candidates) {
        if (Test-CompilerPath $p) { return (Resolve-Path -LiteralPath $p).Path }
    }
    foreach ($name in @('clang', 'armclang')) {
        try {
            $cmd = Get-Command $name -ErrorAction Stop
            if ($cmd.Path) { return $cmd.Path }
        } catch { }
    }
    return $null
}

$idePath = if ($IdeCompiler) { $IdeCompiler } else { $null }
$clangLike = Get-ClangLikeCompiler -Explicit $idePath -Ws $workspace

if ($clangLike) {
    $CompilerPath = $clangLike
    $CompilerArgList = $clangArgList
    Write-Output "IDE indexer: using Clang-compatible compiler:"
    Write-Output "  $CompilerPath"
} else {
    $CompilerPath = $KeilArmccPath
    $CompilerArgList = $armccArgList
    Write-Warning @"
No Clang-compatible compiler found (TIKONG_CLANG_EXE, armclang under Keil, or LLVM clang in PATH/Program Files).
Using armcc in compile_commands — Microsoft IntelliSense may be weak; prefer LLVM/clang.exe or ARM Compiler 6 (armclang).
Install LLVM (clang) or ARM Compiler 6 (armclang), or set environment variable TIKONG_CLANG_EXE to clang/armclang full path, then re-run fix_code_index.bat.
"@
}

$intelSenseModeForIde = if ($clangLike) { 'clang-arm' } else { 'gcc-arm' }
# 不显式写入 c_cpp_properties 的 compilerPath：避免 MSVC 插件用宿主 compilerPath「覆盖」
# compile_commands 的编译器查询（参见 vscode-cpptools #11889），导致 ARM TU 解析失败/F12 失效。

# 使用 -Filter 递归枚举；在部分 PowerShell 版本下 -Include *.c 会漏扫子目录
$sourceFiles = Get-ChildItem -Path $workspace -Recurse -Filter *.c -File -ErrorAction SilentlyContinue | Sort-Object FullName

if ($sourceFiles.Count -eq 0) {
    Write-Error "No .c source files found under $workspace"
    exit 1
}

$entries = @()
foreach ($file in $sourceFiles) {
    $src = $file.FullName
    # MSVC C/C++ / compile_commands：使用 arguments 数组，避免 Windows 下 command 字符串引号解析问题
    $arguments = @($CompilerPath) + $CompilerArgList + @($src)
    $entries += [PSCustomObject]@{
        directory = $workspace
        arguments = $arguments
        file      = $src
    }
}

$outPath = Join-Path $workspace $OutputFile
$json = ($entries | ConvertTo-Json -Depth 8)
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($outPath, $json + "`r`n", $utf8NoBom)

Write-Output "Generated '$OutputFile' with $($sourceFiles.Count) source files (UTF-8, no BOM)."

$vscodeDir = Join-Path $workspace ".vscode"
$cppPropsPath = Join-Path $vscodeDir "c_cpp_properties.json"
$cppProps = @"
{
  "configurations": [
    {
      "name": "Win32",
      "compileCommands": "`${workspaceFolder}/compile_commands.json",
      "forcedInclude": [
        "`${workspaceFolder}/USER/vscode_intellisense_compat.h"
      ],
      "intelliSenseMode": "$intelSenseModeForIde",
      "cStandard": "c99",
      "browse": {
        "path": [
          "`${workspaceFolder}/**"
        ],
        "limitSymbolsToIncludedHeaders": false
      }
    }
  ],
  "version": 4
}
"@
if (-not (Test-Path $vscodeDir)) {
    New-Item -ItemType Directory -Path $vscodeDir | Out-Null
}

$newCfg = (($cppProps.TrimEnd()) | ConvertFrom-Json).configurations[0]
$finalJson = $null
if (Test-Path $cppPropsPath) {
    try {
        $old = Get-Content $cppPropsPath -Raw | ConvertFrom-Json
        $mergedCfgs = [System.Collections.Generic.List[object]]::new()
        $replaced = $false
        foreach ($cfg in $old.configurations) {
            if (($cfg.name -eq "STM32-Keil-DB") -or ($cfg.name -eq "Win32")) {
                $mergedCfgs.Add($newCfg)
                $replaced = $true
            } else {
                $mergedCfgs.Add($cfg)
            }
        }
        if (-not $replaced) {
            $mergedCfgs.Insert(0, $newCfg)
        }
        $version = $old.version
        if (($null -eq $version) -or ($version -eq "")) { $version = 4 }
        $finalObj = [PSCustomObject]@{ configurations = @($mergedCfgs); version = $version }
        $finalJson = ($finalObj | ConvertTo-Json -Depth 100)
    } catch {
        $finalJson = $null
    }
}
if (-not $finalJson) {
    $finalJson = $cppProps.TrimEnd()
}
[System.IO.File]::WriteAllText($cppPropsPath, $finalJson.TrimEnd() + "`r`n", $utf8NoBom)
Write-Output "Wrote '.vscode/c_cpp_properties.json' (Win32 / compile_commands block merged)."
Write-Output "Tip (Microsoft C/C++): Ctrl+Shift+P -> 'C/C++: Reset IntelliSense Database'; optional 'Developer: Reload Window'."
Write-Output "NOTE: IntelliSense options live in-repo at '.vscode/settings.json' — this script does not modify them."
