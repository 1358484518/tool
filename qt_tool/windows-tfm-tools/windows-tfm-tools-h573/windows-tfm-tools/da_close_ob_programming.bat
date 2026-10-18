@echo off
rem 本文件是 ob_programming.bat 的副本，只给 da_tfm_close.bat 调用。
rem 不改原来的 ROT_Provisioning\DA\ob_programming.bat。
rem
rem 对应手工流程里 provisioning.bat 在“请下载程序”之前的那一步。
rem 这里的 SECBOOTADD=0xC0000 只是 DA 初始值。下一步 da_close_tfm_update.bat
rem 会再把它改成 TF-M 的 0xC0100，并烧录 bl2 / S / NS。
rem
rem 和原脚本的差别：
rem 1. 先 Under Reset 把 PRODUCT_STATE 设为 OPEN(0xED)。不是 OPEN 时 SECWM / SECBOOTADD 写不进去。
rem 2. 清保护和擦除也用 Under Reset，避免 OB_LAUNCH 之后应用程序把选项字节改回去。
rem 3. 原脚本在括号里写 if %errorlevel%，百分号会提前展开成 0，编程失败也会继续往下走。
rem    这里每条命令执行完立刻用 if errorlevel 1 判断。
call ../env.bat

if "%~1"=="" (set /p "tzen_state= The TrustZone feature is enabled ? [ y | n ]:") else (set "tzen_state=%~1")
if /i "%tzen_state%"=="y" (set "tzen_ob=0xB4") else (set "tzen_ob=0xC3")

rem Under Reset：按住内核，应用程序不能在选项字节生效后把值改掉。
set "connect_reset=-c port=SWD speed=fast ap=1 mode=UR"
rem Hotplug：Under Reset 连不上时的补救。日志里复位失败前，Hotplug 是能连上的。
set "connect_hotplug=-c port=SWD speed=fast ap=1 mode=Hotplug"

set "sec_water_mark=-ob SECWM1_STRT=0x0 SECWM1_END=0x7F SECWM2_STRT=0x0 SECWM2_END=0x7F"
set "remove_protect_init_non_secure=-ob WRPSGn1=0xffffffff WRPSGn2=0xffffffff HDP1_STRT=1 HDP1_END=0 HDP2_STRT=1 HDP2_END=0"
set "remove_protect_init_secure=-ob SECWM1_STRT=1 SECWM1_END=0 WRPSGn1=0xffffffff WRPSGn2=0xffffffff SECWM2_STRT=1 SECWM2_END=0 HDP1_STRT=1 HDP1_END=0 HDP2_STRT=1 HDP2_END=0 SECBOOT_LOCK=0xC3 SWAP_BANK=0 SRAM2_RST=0 SRAM2_ECC=0 NSBOOTADD=0x80000 SECBOOTADD=0xC0000 BOOT_UBE=0xB4"
set "erase_all=-e all"

rem 产品状态先回到 OPEN。0xED=OPEN，TZEN 0xB4=打开。
set "action=Set PRODUCT_STATE OPEN and TZEN under reset"
echo %action%
echo CMD: %stm32programmercli% %connect_reset% -ob PRODUCT_STATE=0xED TZEN=%tzen_ob%
%stm32programmercli% %connect_reset% -ob PRODUCT_STATE=0xED TZEN=%tzen_ob%
if not errorlevel 1 goto :open_ok
echo [warn] Under Reset 连不上，改用 Hotplug 再打开一次产品状态。
echo CMD: %stm32programmercli% %connect_hotplug% -ob PRODUCT_STATE=0xED TZEN=%tzen_ob%
%stm32programmercli% %connect_hotplug% -ob PRODUCT_STATE=0xED TZEN=%tzen_ob%
if errorlevel 1 goto :error
:open_ok

rem TrustZone 打开时先拆掉保护并整片擦除，仍然按住复位。
set "action=Configure Initial OB under reset"
echo %action%
if /i "%tzen_state%"=="n" goto :init_nonsecure
echo CMD: %stm32programmercli% %connect_reset% %remove_protect_init_secure% %erase_all%
%stm32programmercli% %connect_reset% %remove_protect_init_secure% %erase_all%
if errorlevel 1 goto :error
goto :watermark

:init_nonsecure
echo CMD: %stm32programmercli% %connect_reset% %remove_protect_init_non_secure% %erase_all%
%stm32programmercli% %connect_reset% %remove_protect_init_non_secure% %erase_all%
if errorlevel 1 goto :error
goto :end_programming

:watermark
rem 擦除之后再把两片 Bank 标成 Secure。这一步失败就停，不再往下连。
set "action=Configure Secure Water Mark"
echo %action%
echo CMD: %stm32programmercli% %connect_reset% %sec_water_mark%
%stm32programmercli% %connect_reset% %sec_water_mark%
if errorlevel 1 goto :error

:end_programming
echo Programming success
if [%2] neq [AUTO] cmd /k
exit 0

:error
echo        Error when trying to "%action%"
echo        Programming aborted
if [%2] neq [AUTO] cmd /k
exit 1
