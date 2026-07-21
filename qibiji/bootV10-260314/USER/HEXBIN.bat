::
::date   2023/09/19
::brief  在keil中,生成hex、bin文件到 HEXBIN文件夹中.
::由于当工程文件夹路径存在空格时会找不到正确的路径
::命令进行修改：
::.\HEXBIN.bat $K !L @L $L
::$K KEIL安装目录 !L .axf文件的位置 @L .axf文件的文件名 $L.axf文件的目录

::关闭显示
@echo off
::获取时间
set year=%date:~2,2%
set month=%date:~5,2%
set day=%date:~8,2%
set hour=%time:~0,2%
set min=%time:~3,2%
set sec=%time:~6,2%
if "%time:~0,1%"  == " " (
	set hour=0%time:~1,1%
)
::创建输出文件夹 HEXBIN
if not exist ..\HEXBIN (mkdir ..\HEXBIN
)

::设置fromelf.exe位置
set exe_location=%1ARM\ARMCC\bin\fromelf.exe
::设置.axf文件的位置
set obj_location=%2
::获取工程名
set project_name=%3
::设置.axf文件所在目录路径
set obj_path=%4
::设置输出后的文件名
::set output_name=%project_name%_%year%%month%%day%%hour%%min%
set output_name=%project_name%
::将bin文件生成到HEXBIN文件夹  >nul屏蔽成功命令
%exe_location% --bin -o ..\HEXBIN\%output_name%.bin %obj_location% >nul
::将hex文件重命名
ren %obj_path%%project_name%.hex %output_name%.hex >nul
::将hex文件复制到HEXBIN文件夹
move %obj_path%%output_name%.hex ..\HEXBIN >nul