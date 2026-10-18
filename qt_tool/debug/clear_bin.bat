@echo off
setlocal

rem 切换到脚本所在目录
cd /d "%~dp0"

echo ========================================
echo 将要删除以下 .bin 文件：
echo ========================================
dir /s /b *.bin 2>nul
echo ========================================
echo.

set /p confirm=确认删除以上所有 .bin 文件？(输入 Y 确认，其他取消): 
if /i not "%confirm%"=="Y" (
    echo 已取消，未做任何修改。
    pause
    exit /b
)

del /s /q *.bin 2>nul

echo.
echo 删除完成。
pause
