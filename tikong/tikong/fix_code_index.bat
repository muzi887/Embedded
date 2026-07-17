@echo off
setlocal EnableExtensions
cd /d "%~dp0"

echo [%date% %time%] Fixing code index / IntelliSense workspace files...
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
    exit /b 1
)

echo.
echo Done. Recommended in Cursor / VS Code:
echo   - Ctrl+Shift+P -^> "Clangd: Restart language server"
echo   - Or "Developer: Reload Window"
echo   - If using MS C/C++ extension: "C/C++: Reset IntelliSense Database"
echo.
pause
exit /b 0
