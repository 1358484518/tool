@echo off
rem 把当前连接的 STM32H5F4 选项字节写成出厂默认值。
rem 数值来自同目录 STM32H5F4默认选项字节.json。双击或在命令行调用均可。
rem 连接方式：ST-LINK，SWD，Under Reset。板子 NRST 要接到调试器。
rem 不用 chcp 65001。UTF-8 加括号时，cmd 会把中文拆成命令，所以本文件用系统 ANSI/GBK 保存。
setlocal

rem CubeProgrammer 命令行完整路径
rem Programmer path: argument 1 or CUBEPROG. A directory is accepted as well as the exe.
set "CLI=%~1"
if not defined CLI set "CLI=%CUBEPROG%"
if defined CLI if exist "%CLI%\STM32_Programmer_CLI.exe" set "CLI=%CLI%\STM32_Programmer_CLI.exe"
if defined CLI if exist "%CLI%\bin\STM32_Programmer_CLI.exe" set "CLI=%CLI%\bin\STM32_Programmer_CLI.exe"
if not defined CLI if exist "D:\ST\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" set "CLI=D:\ST\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
if not defined CLI if exist "%ProgramFiles%\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" set "CLI=%ProgramFiles%\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
if not defined CLI if exist "%ProgramFiles(x86)%\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" set "CLI=%ProgramFiles(x86)%\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
if not exist "%CLI%" goto :nocli
echo STM32_Programmer_CLI: %CLI%

echo 正在通过 SWD 写入 STM32H5F4 出厂选项字节...
echo 产品状态保持 Open。不会写成 Closed 或 Locked。

rem 先写除 Bank2 水印以外的出厂值。水印单独写，避免它失败时把整次编程判失败。
call :write_factory
if not errorlevel 1 goto :watermark

echo.
echo 安全相关选项字节没有生效。TrustZone 仍开着时，隐藏区里的程序会在复位后把选项字节改回去。
echo 接下来先切到系统存储区并关闭 TrustZone，再关掉隐藏保护和写保护，然后整片擦除 Flash。
call :tz_recover
if errorlevel 1 goto :fail
call :write_factory
if errorlevel 1 goto :fail

:watermark
rem Bank2 水印一旦被收成空安全区，硬件就不允许再加大。TrustZone 关闭时这项不参与保护。
echo.
echo 尝试恢复 Bank2 水印 SECWM2...
call :write_secwm2
if errorlevel 1 goto :wm_keep

echo.
echo 回读选项字节...
call :read_ob
if errorlevel 1 goto :fail
echo.
echo 完成。
goto :end

:wm_keep
echo Bank2 水印仍是 SECWM2_STRT=0xFF SECWM2_END=0x0。
echo TrustZone 已关闭时，这块水印不生效，其余选项字节按出厂值写入。
echo.
echo 回读选项字节...
call :read_ob
if errorlevel 1 goto :fail
echo.
echo 完成。Bank2 水印无法退回出厂编码。
goto :end

rem 写出厂选项字节，不包含 SECWM2，也不写 SECBOOT_LOCK。
rem SECBOOT_LOCK 在 TrustZone 关闭时读出为 0，命令行不接受 SECBOOT_LOCK=0x0。
:write_factory
"%CLI%" -c port=SWD mode=UR -ob PRODUCT_STATE=0xED BOR_LEV=0x0 BORH_EN=0x0 IO_VDD_HSLV=0x0 IO_VDDIO2_HSLV=0x0 IWDG_STOP=0x1 IWDG_STDBY=0x1 IWDG_SW=0x1 BOOT_UBE=0xB4 SWAP_BANK=0x0 NRST_STOP=0x1 NRST_STDBY=0x1 TZEN=0xC3 USBPD_DIS=0x1 SRAM2_ECC=0x1 SRAM3_ECC=0x1 BKPRAM_ECC=0x1 SRAM2_RST=0x1 SRAM1_3_4_5_RST=0x1 NSBOOTADD=0x80000 NSBOOT_LOCK=0xC3 SECBOOTADD=0x0 LOCKBL=0x0 WRPSG12=0xFFFFFFFF WRPSG11=0xFFFFFFFF WRPSG22=0xFFFFFFFF WRPSG21=0xFFFFFFFF SECWM1_STRT=0x0 SECWM1_END=0xFF EDATA1_EN=0x0 EDATA1_STRT=0x0 EDATA2_EN=0x0 EDATA2_STRT=0x0 HDP1_STRT=0x1 HDP1_END=0x0 HDP2_STRT=0x1 HDP2_END=0x0
if errorlevel 1 exit /b 1
exit /b 0

rem TrustZone 打开时的恢复步骤。会擦除全部用户 Flash。
rem 先启动到系统存储区，避免用户区里的安全程序在 OB_LAUNCH 之后改回选项字节。
:tz_recover
"%CLI%" -c port=SWD mode=UR -ob BOOT_UBE=0xC3 TZEN=0xC3 SRAM2_ECC=0x1 SRAM2_RST=0x1
if errorlevel 1 exit /b 1
"%CLI%" -c port=SWD mode=UR -ob WRPSG11=0xFFFFFFFF WRPSG12=0xFFFFFFFF WRPSG21=0xFFFFFFFF WRPSG22=0xFFFFFFFF HDP1_STRT=0x1 HDP1_END=0x0 HDP2_STRT=0x1 HDP2_END=0x0
if errorlevel 1 exit /b 1
"%CLI%" -c port=SWD mode=UR -e all
if errorlevel 1 exit /b 1
"%CLI%" -c port=SWD mode=UR -ob BOOT_UBE=0xB4
if errorlevel 1 exit /b 1
exit /b 0

rem 单独写 Bank2 水印。失败时由调用处决定是否继续。
:write_secwm2
"%CLI%" -c port=SWD mode=UR -ob SECWM2_STRT=0x0 SECWM2_END=0xFF
if errorlevel 1 exit /b 1
exit /b 0

rem 回读全部选项字节，方便和默认 json 对照。
:read_ob
"%CLI%" -c port=SWD mode=UR -ob displ
if errorlevel 1 exit /b 1
exit /b 0

:nocli
echo 找不到 STM32_Programmer_CLI.exe
echo %CLI%
pause
exit /b 1

:fail
echo.
echo 写入失败。请确认 ST-LINK 已连接，芯片有供电，NRST 已接到调试器。
pause
exit /b 1

:end
pause
exit /b 0
