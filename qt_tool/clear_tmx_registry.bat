@echo off
chcp 936 >nul
echo 将删除: HKCU\Software\TMX\TMX_TOOL
echo 这里有下载路径、语言、日志目录和固件页记录。
echo 请先关掉调试助手。
set /p ANSWER=确认删除请输入 Y:
if /I not "%ANSWER%"=="Y" (
    echo 已取消。
    echo.
    pause
    exit /b 0
)
reg query "HKCU\Software\TMX\TMX_TOOL" >nul 2>&1
if errorlevel 1 (
    echo 没有找到 TMX_TOOL 的注册信息。
    echo.
    pause
    exit /b 0
)
reg delete "HKCU\Software\TMX\TMX_TOOL" /f
if errorlevel 1 (
    echo 删除失败。
    echo.
    pause
    exit /b 1
)
echo 已删除。下次打开程序会按新安装目录重新记录路径。
echo.
pause
