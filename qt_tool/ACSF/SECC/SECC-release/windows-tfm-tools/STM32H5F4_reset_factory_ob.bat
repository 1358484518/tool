@echo off
rem 把当前连接的 STM32H5F4 选项字节写成出厂默认值。
rem 数值来自同目录 STM32H5F4默认选项字节.json。双击或在命令行调用均可。
rem 连接方式：ST-LINK，SWD，Under Reset。板子 NRST 要接到调试器。
rem 写出厂值最多 3 次。成功后再整片擦除。三次都失败则不擦 Flash。
rem 不用 chcp 65001。UTF-8 加括号时，cmd 会把中文拆成命令，所以本文件用系统 ANSI/GBK 保存。
setlocal

rem CubeProgrammer 命令行完整路径
set "CLI=D:\ST\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"

if not exist "%CLI%" goto :nocli

echo 正在通过 SWD 写入 STM32H5F4 出厂选项字节...
echo 产品状态保持 Open。不会写成 Closed 或 Locked。

rem 写出厂值最多 3 次。失败只解锁，不擦 Flash。三次都失败就退出。
set "TRY=0"

:retry_factory
set /a TRY+=1
echo.
echo 第 %TRY%/3 次写出厂选项字节...
call :write_factory
if not errorlevel 1 goto :watermark
if %TRY% GEQ 3 goto :fail
echo 第 %TRY% 次失败。解开启动锁和隐藏保护后再试，先不擦除 Flash。
call :tz_unlock
goto :retry_factory

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
set "DONE_MSG=完成。用户 Flash 已整片擦除。"
goto :erase_then_done

:wm_keep
echo Bank2 水印仍是 SECWM2_STRT=0xFF SECWM2_END=0x0。
echo TrustZone 已关闭时，这块水印不生效，其余选项字节按出厂值写入。
echo.
echo 回读选项字节...
call :read_ob
if errorlevel 1 goto :fail
set "DONE_MSG=完成。Bank2 水印无法退回出厂编码。用户 Flash 已整片擦除。"
goto :erase_then_done

rem 出厂选项字节已经写上，这时才整片擦除用户 Flash。
:erase_then_done
echo.
echo 选项字节已写成出厂值，开始整片擦除 Flash。
call :erase_flash
if not errorlevel 1 goto :erase_ok
set "FAIL_MSG=选项字节已写成出厂值，但整片擦除失败。"
goto :fail
:erase_ok
echo.
echo %DONE_MSG%
goto :end

rem 写出厂选项字节，不包含 SECWM2，也不写 SECBOOT_LOCK。
rem SECBOOT_LOCK 在 TrustZone 关闭时读出为 0，命令行不接受 SECBOOT_LOCK=0x0。
:write_factory
"%CLI%" -c port=SWD mode=UR -ob PRODUCT_STATE=0xED BOR_LEV=0x0 BORH_EN=0x0 IO_VDD_HSLV=0x0 IO_VDDIO2_HSLV=0x0 IWDG_STOP=0x1 IWDG_STDBY=0x1 IWDG_SW=0x1 BOOT_UBE=0xB4 SWAP_BANK=0x0 NRST_STOP=0x1 NRST_STDBY=0x1 TZEN=0xC3 USBPD_DIS=0x1 SRAM2_ECC=0x1 SRAM3_ECC=0x1 BKPRAM_ECC=0x1 SRAM2_RST=0x1 SRAM1_3_4_5_RST=0x1 NSBOOTADD=0x80000 NSBOOT_LOCK=0xC3 SECBOOTADD=0x0 LOCKBL=0x0 WRPSG12=0xFFFFFFFF WRPSG11=0xFFFFFFFF WRPSG22=0xFFFFFFFF WRPSG21=0xFFFFFFFF SECWM1_STRT=0x0 SECWM1_END=0xFF EDATA1_EN=0x0 EDATA1_STRT=0x0 EDATA2_EN=0x0 EDATA2_STRT=0x0 HDP1_STRT=0x1 HDP1_END=0x0 HDP2_STRT=0x1 HDP2_END=0x0
if errorlevel 1 exit /b 1
exit /b 0

rem 一次写出厂值失败后调用。解开启动锁并关掉隐藏保护，给下一次写入用。不擦 Flash。
:tz_unlock
"%CLI%" -c port=SWD mode=UR -ob BOOT_UBE=0xC3 TZEN=0xC3 SRAM2_ECC=0x1 SRAM2_RST=0x1
"%CLI%" -c port=SWD mode=UR -ob WRPSG11=0xFFFFFFFF WRPSG12=0xFFFFFFFF WRPSG21=0xFFFFFFFF WRPSG22=0xFFFFFFFF HDP1_STRT=0x1 HDP1_END=0x0 HDP2_STRT=0x1 HDP2_END=0x0
"%CLI%" -c port=SWD mode=UR -ob BOOT_UBE=0xB4
exit /b 0

rem 出厂选项字节写成功之后整片擦除用户 Flash。
:erase_flash
"%CLI%" -c port=SWD mode=UR -e all
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
if not defined FAIL_MSG set "FAIL_MSG=三次写出厂选项字节都失败，未擦除 Flash。请确认 ST-LINK 已连接，芯片有供电，NRST 已接到调试器。"
echo %FAIL_MSG%
pause
exit /b 1

:end
pause
exit /b 0
