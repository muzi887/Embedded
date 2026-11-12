@echo off
for /f %%i in ('%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe -NoProfile -Command "Get-Date -Format yyMMddHHmm"') do set stamp=%%i
if not exist ..\HEXBIN (mkdir ..\HEXBIN
)

set exe_location=%1ARM\ARMCC\bin\fromelf.exe
set obj_location=%2
set project_name=%3
set obj_path=%4
set output_name=%project_name%_%stamp%
%exe_location% --bin -o ..\HEXBIN\%output_name%.bin %obj_location% >nul
ren %obj_path%%project_name%.hex %output_name%.hex >nul
move %obj_path%%output_name%.hex ..\HEXBIN >nul

copy /y ..\HEXBIN\%output_name%.bin ..\HEXBIN\%project_name%.bin >nul
copy /y ..\HEXBIN\%output_name%.hex ..\HEXBIN\%project_name%.hex >nul
