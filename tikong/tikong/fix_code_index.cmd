@echo off
setlocal EnableExtensions
cd /d "%~dp0"

title tikong: compile_commands + MSVC IntelliSense caches
echo.
echo [%date% %time%] Fixing code index (compile_commands + stale caches)...
echo Project root: %CD%
echo.

set "PWSH=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%PWSH%" goto run_ps
set "PWSH=%SystemRoot%\SysWOW64\WindowsPowerShell\v1.0\powershell.exe"
if exist "%PWSH%" goto run_ps
where powershell >nul 2>&1
if errorlevel 1 (
    echo ERROR: PowerShell not found. Expected at:
    echo   %SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe
    exit /b 1
)
set "PWSH=powershell.exe"

:run_ps
"%PWSH%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0generate_compile_commands.ps1"
if errorlevel 1 (
    echo.
    echo FAILED: generate_compile_commands.ps1 exited with error.
    echo Cache was NOT cleared so your previous index is left untouched.
    exit /b 1
)

echo.
echo Clearing stale indexer caches ^(optional .cache/clangd + MS Browse DB + ipch^)...

if exist "%~dp0.cache\clangd" (
    rmdir /s /q "%~dp0.cache\clangd"
    echo   Removed: .cache\clangd
) else (
    echo   Skip: no .cache\clangd
)

if exist "%~dp0.vscode\ipch" (
    rmdir /s /q "%~dp0.vscode\ipch"
    echo   Removed: .vscode\ipch
) else (
    echo   Skip: no .vscode\ipch
)

REM Microsoft C/C++ Browse database （长期会陈旧；编辑器正在打开该工作区时可能无法删除）
if exist "%~dp0.vscode\BROWSE.VC.DB*" (
    del /f /q "%~dp0.vscode\BROWSE.VC.DB*" >nul 2>&1
    if exist "%~dp0.vscode\BROWSE.VC.DB" (
        echo   Warn: could not delete .vscode\BROWSE.VC.DB ^- close Cursor/VS Code and run again if needed.
    ) else (
        echo   Removed: .vscode\BROWSE.VC.DB*
    )
) else (
    echo   Skip: no .vscode\BROWSE.VC.DB
)

echo.
echo OK: compile_commands regenerated and caches cleared.
echo Tip: IDE database prefers LLVM\bin\clang.exe when installed ^(fallback: Keil armclang^); see generate_compile_commands.ps1.
echo.
echo In Cursor / VS Code ^(workspace folder MUST be this directory^):
echo   [1] Install extension: ms-vscode.cpptools ^(Microsoft C/C++^) if missing.
echo   [2] Ctrl+Shift+P - "C/C++: Reset IntelliSense Database"
echo       then "Developer: Reload Window" if F12 still bad.
echo.

REM Let VS Code / CI run this script non-interactively: set SKIP_CODE_INDEX_PAUSE=1
if not defined SKIP_CODE_INDEX_PAUSE (
    pause
)
exit /b 0
