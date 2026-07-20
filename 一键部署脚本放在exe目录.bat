@echo off
setlocal

:: 1. 临时设置Qt环境变量（仅当前脚本窗口生效）
set QT_ROOT=D:\Qt\Qt5.14.2\5.14.2\mingw73_64
set "PATH=%QT_ROOT%\bin;%PATH%"
set "QT_PLUGIN_PATH=%QT_ROOT%\plugins"

:: 切换到脚本所在目录（exe所在目录）
cd /d "%~dp0"

:: 自动获取当前目录下exe
set APP_EXE=
for %%f in (*.exe) do set APP_EXE=%%f
if not defined APP_EXE (
    echo Error: No exe file in current folder
    pause
    exit /b
)

:: 2. 当前目录执行windeployqt
windeployqt  "%APP_EXE%"

pause
