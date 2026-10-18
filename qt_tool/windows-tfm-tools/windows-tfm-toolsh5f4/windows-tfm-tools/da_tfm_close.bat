@echo off
rem ****************************************************************************
rem  STM32H5F4 / H5F5 一条龙：先烧 TF-M，再做 DA 保护。
rem  不修改本目录已有脚本，TF-M 烧录只调用 tfm_update.bat。
rem
rem  顺序：
rem    1. 产品状态回到 OPEN（0xED）。已经是 OPEN 时再写一次。
rem       先 Under Reset；连不上再 Hotplug。Provisioning 状态下 Under Reset
rem       会报 Cannot connect to access port 1，Hotplug 仍可写选项字节。
rem    2. tfm_update.bat
rem       擦除，写 TF-M 选项字节（SECBOOTADD=0xC0100、BOOT_UBE=0xB4、
rem       SECWM 0-255、WRPSG11/12/21/22），下载 BL2 / S / NS，然后复位。
rem    3. PRODUCT_STATE=0x17（Provisioning）。写 obk 之前必须在这个状态。
rem       BL2 正在跑时 Hotplug 会在关 ICache 处失败：
rem       ICache disable failed / All the 14 configurations are disabled
rem       这时选项字节还没写。先 Under Reset，失败再 Hotplug 重试。
rem    4. 烧 DA_Config.obk（-sdp）。TrustZone 关闭时改用带密码的 obk。
rem    5. 把 SECBOOTADD 写回 0xC0100，并读回来核对。
rem       实测只写 PRODUCT_STATE=0x17 后 SECBOOTADD 会变成 0x0。
rem       不写回的话，CLOSED 之后 TF-M 不会从 BL2 启动。
rem    6. PRODUCT_STATE=0x72（CLOSED）。调试口此后要用 DA 才能再打开。
rem       写完连接会掉，这一步看到 “successfully programmed” 即算完成。
rem
rem  不调用 Cube 的 provisioning.bat / ob_programming.bat。
rem  那份脚本使用 WRPSGn1（本芯片不存在），并把 SECBOOTADD 设成 0xC0000。
rem
rem  用法：
rem    da_tfm_close.bat
rem    da_tfm_close.bat <ST-LINK 序列号>
rem ****************************************************************************
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"
title STM32H5F4 DA + TF-M CLOSE

rem ============================================================================
rem  只改下面这些配置。
rem ============================================================================
rem 每个步骤开始前等待按键的秒数。按任意键立刻继续。范围 1 到 99999。
set "STEP_WAIT_SEC=10"
rem Hotplug 撞上 ICache disable failed 时的重试次数。
set "OB_RETRY=5"
rem y = TrustZone 打开，使用 DA_Config.obk。n = DA_ConfigWithPassword.obk。
set "TZEN_STATE=y"
set "TZEN_OB=0xB4"
rem 最终产品状态。CLOSED=0x72。TZ-CLOSED 是 0xC6，不要和这里混。
rem LOCKED=0x5C 不可逆，本脚本不写 LOCKED。
set "FINAL_PS_NAME=CLOSED"
set "FINAL_PS_VALUE=0x72"
set "PROVISIONING_PS_VALUE=0x17"
set "OPEN_PS_VALUE=0xED"
rem TF-M BL2 入口。0xC0100 = 0x0C010000。
set "TFM_SECBOOTADD=0xC0100"
set "TFM_BOOT_UBE=0xB4"

rem obk、证书、回归脚本都在本目录 ROT_Provisioning\DA，用相对路径。
set "DA_DIR=%~dp0ROT_Provisioning\DA"

set "FAILED_STEP="
set "SN_ARG="
if not "%~1"=="" set "SN_ARG=%~1"
set "SN_OPT="
if defined SN_ARG set "SN_OPT=sn=%SN_ARG%"
set "CONNECT_UR=-c port=SWD speed=fast ap=1 %SN_OPT% mode=UR"
set "CONNECT_HP=-c port=SWD speed=fast ap=1 %SN_OPT% mode=Hotplug"
set "LOG=%~dp0da_tfm_close.log"
set "STEPLOG=%TEMP%\h5f4_da_step.log"

if /i "%TZEN_STATE%"=="y" (
    set "DA_OBK_NAME=DA_Config.obk"
) else (
    set "TZEN_OB=0xC3"
    set "DA_OBK_NAME=DA_ConfigWithPassword.obk"
)
set "DA_OBK=%DA_DIR%\Binary\!DA_OBK_NAME!"

echo.>"%LOG%"
echo.
echo ============================================================
echo  STM32H5F4  TF-M 烧录 + DA %FINAL_PS_NAME%
echo  每步等待: %STEP_WAIT_SEC% 秒（脚本开头 STEP_WAIT_SEC）
echo  TrustZone: %TZEN_STATE%
echo  obk: !DA_OBK!
echo  日志: %LOG%
echo ============================================================
echo.

if exist "D:\ST\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" set "PATH=D:\ST\STM32CubeProgrammer\bin;%PATH%"
call "%~dp0h5f4_env.bat"
call "%~dp0h5f4_setup.bat"
if errorlevel 1 (
    set "FAILED_STEP=找不到 STM32_Programmer_CLI"
    goto :fail
)
set "CLI="
for /f "delims=" %%I in ('where STM32_Programmer_CLI') do (
    if not defined CLI set "CLI=%%I"
)
if not defined CLI (
    set "FAILED_STEP=找不到 STM32_Programmer_CLI"
    goto :fail
)
echo [ok]  !CLI!
echo.

if not exist "!DA_OBK!" (
    echo [FAIL] 没有 !DA_OBK!
    echo        用 TrustedPackageCreator 打开
    echo        %DA_DIR%\Config\DA_Config.xml
    echo        生成 !DA_OBK_NAME! 到 %DA_DIR%\Binary\ 后再运行。
    set "FAILED_STEP=缺少 !DA_OBK_NAME!"
    goto :fail
)
echo [ok]  已找到 !DA_OBK_NAME!
echo.

rem ----- 1/6 OPEN。tfm_update 里的 regression 用 Under Reset，芯片必须先能被复位连接。
set "FAILED_STEP=1/6 产品状态 OPEN"
call :wait_step "%FAILED_STEP%"
set "CLI_ARGS=%CONNECT_UR% -ob PRODUCT_STATE=%OPEN_PS_VALUE% TZEN=%TZEN_OB%"
call :run_logged
if not errorlevel 1 goto :open_ok
echo [warn] Under Reset 未写成，改用 Hotplug。
set "CLI_ARGS=%CONNECT_HP% -ob PRODUCT_STATE=%OPEN_PS_VALUE% TZEN=%TZEN_OB%"
call :run_retry
if errorlevel 1 goto :fail
:open_ok
echo [ok]  PRODUCT_STATE=%OPEN_PS_VALUE%
echo.

rem ----- 2/6 现有 tfm_update.bat。H5F4_INNER=1 时它不会再套一层 cmd，也不会 pause。
set "FAILED_STEP=2/6 tfm_update.bat 烧录 TF-M"
call :wait_step "%FAILED_STEP%"
set "H5F4_INNER=1"
if defined SN_ARG (
    call "%~dp0tfm_update.bat" %SN_ARG%
) else (
    call "%~dp0tfm_update.bat"
)
if errorlevel 1 goto :fail
echo [ok]  tfm_update.bat 完成
echo.

rem ----- 3/6 PROVISIONING。Under Reset 按住内核，避免 BL2 正在跑时关 ICache 失败。
set "FAILED_STEP=3/6 产品状态 PROVISIONING"
call :wait_step "%FAILED_STEP%"
set "CLI_ARGS=%CONNECT_UR% -ob PRODUCT_STATE=%PROVISIONING_PS_VALUE%"
call :run_logged
if not errorlevel 1 goto :prov_ok
echo [warn] Under Reset 未写成。BL2 在跑时 Hotplug 可能报 ICache disable failed，将重试。
set "CLI_ARGS=%CONNECT_HP% -ob PRODUCT_STATE=%PROVISIONING_PS_VALUE%"
call :run_retry
if errorlevel 1 goto :fail
:prov_ok
echo [ok]  PRODUCT_STATE=%PROVISIONING_PS_VALUE%
echo.

rem ----- 4/6 obk。此时 SECBOOTADD 往往已是 0x0，复位不会进 BL2，-sdp 用 Hotplug。
set "FAILED_STEP=4/6 写入 !DA_OBK_NAME!"
call :wait_step "%FAILED_STEP%"
set "CLI_ARGS=%CONNECT_HP% -hardRst"
call :run_retry
if errorlevel 1 goto :fail
set "CLI_ARGS=%CONNECT_HP% -sdp !DA_OBK!"
call :run_retry
if errorlevel 1 goto :fail
echo [ok]  obk 已写入
echo.

rem ----- 5/5 离开 PROVISIONING 时，SECBOOTADD 和最终状态必须同一条命令写。
rem 还停在 PROVISIONING 时单独写 SECBOOTADD，OB_LAUNCH 之后会变回 0x0。
rem 写完调试口会断开。重连失败是 CLOSED 的正常结果，不算失败。
rem 若日志写明 secbootadd found: 0x0，启动地址没留住，这一步失败。
set "FAILED_STEP=5/5 写回 SECBOOTADD=%TFM_SECBOOTADD% 并进入 %FINAL_PS_NAME%"
call :wait_step "%FAILED_STEP%"
echo 即将写入 SECBOOTADD=%TFM_SECBOOTADD% 和 %FINAL_PS_NAME% ^(%FINAL_PS_VALUE%^)。
echo 写完调试口会断开。
set "CLI_ARGS=%CONNECT_HP% -ob SECBOOTADD=%TFM_SECBOOTADD% BOOT_UBE=%TFM_BOOT_UBE% PRODUCT_STATE=%FINAL_PS_VALUE%"
call :run_final
if errorlevel 1 goto :fail
echo.
echo ============================================================
echo  已做完。TF-M 已烧录，DA obk 已写入，产品状态为 %FINAL_PS_NAME% ^(%FINAL_PS_VALUE%^)。
echo  SECBOOTADD=%TFM_SECBOOTADD% 与最终状态同一次写入。
echo  之后不要再跑 tfm_update.bat：它会把产品状态写回 OPEN。
echo  要退回 Open：双击 ROT_Provisioning\DA\regression.bat，再用本目录 tfm_update.bat。
echo ============================================================
goto :stay_open

:run_retry
set "TRY=0"
:run_retry_loop
set /a TRY+=1
echo [info] 第 !TRY! / %OB_RETRY% 次
call :run_logged
if not errorlevel 1 exit /b 0
findstr /I /C:"ICache disable failed" /C:"no active configuration" /C:"Cannot connect to access port" "%STEPLOG%" >nul
if errorlevel 1 exit /b 1
if !TRY! GEQ %OB_RETRY% exit /b 1
echo [warn] 连接未完成，2 秒后重试。
timeout /t 2 /nobreak >nul
goto :run_retry_loop

rem 最终状态：编程成功，或写完后调试口断开，都算完成。
rem secbootadd 读回 0x0 算失败。只出现 ICache 错误时重试。
:run_final
set "TRY=0"
:run_final_loop
set /a TRY+=1
echo [info] 第 !TRY! / %OB_RETRY% 次
call :run_logged
findstr /I /C:"found: 0x0" "%STEPLOG%" >nul
if not errorlevel 1 exit /b 1
findstr /I /C:"Option Bytes successfully programmed" /C:"failed to reconnect after reset" /C:"Unable to reconnect after setting the Option Bytes" "%STEPLOG%" >nul
if not errorlevel 1 exit /b 0
findstr /I /C:"ICache disable failed" /C:"no active configuration" /C:"Cannot connect to access port" "%STEPLOG%" >nul
if errorlevel 1 exit /b 1
if !TRY! GEQ %OB_RETRY% exit /b 1
echo [warn] 连接未完成，2 秒后重试。
timeout /t 2 /nobreak >nul
goto :run_final_loop

:run_logged
echo CMD: "%CLI%" !CLI_ARGS!
echo CMD: "%CLI%" !CLI_ARGS!>>"%LOG%"
"%CLI%" !CLI_ARGS! > "%STEPLOG%" 2>&1
set "RC=!ERRORLEVEL!"
type "%STEPLOG%"
type "%STEPLOG%" >> "%LOG%"
if not "!RC!"=="0" exit /b 1
exit /b 0

:wait_step
echo ------------------------------------------------------------
echo 下一步: %~1
echo 等待 %STEP_WAIT_SEC% 秒。按任意键立即继续。
echo ------------------------------------------------------------
timeout /t %STEP_WAIT_SEC%
echo.
exit /b 0

:fail
echo.
echo ============================================================
echo  失败，已停下: %FAILED_STEP%
echo  后面的步骤没有执行。日志: %LOG%
echo ============================================================
goto :stay_open

:stay_open
echo.
echo 窗口保持打开。输入 exit 或关闭窗口即可结束。
cmd /k
