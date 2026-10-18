@echo off
rem ****************************************************************************
rem  从 PROVISIONING 接着做，不擦除、不重烧 TF-M。
rem
rem  给 da_tfm_close.bat 停在“产品状态 PROVISIONING”之后用。
rem  不要再双击 da_tfm_close.bat，那会整片擦除并重新烧录。
rem
rem  顺序：
rem    1. 再确认 PRODUCT_STATE=0x17（已经是 PROVISIONING 时，这条只是核对）
rem    2. 写入 ROT_Provisioning\DA\Binary\DA_Config.obk
rem    3. 产品状态设为 CLOSED（0x72）。写完调试口会断，返回码不当成失败。
rem
rem  用法：双击本文件。TrustZone 默认开。
rem ****************************************************************************
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"
title STM32H573 接着写 obk 然后 CLOSED

rem ============================================================================
rem  STEP_WAIT_SEC
rem  每个步骤开始前等待按键的秒数。只改下面这一行。
rem  按任意键立刻开始这一步；秒数走完仍然没按键，也会开始这一步。
rem ============================================================================
set "STEP_WAIT_SEC=30"

rem TrustZone 打开时用 DA_Config.obk。改成 n 才会用带密码的 obk。
set "TZEN_STATE=y"
rem CLOSED=0x72。这一步会关掉调试口。
set "FINAL_PS_VALUE=0x72"
rem 写 obk 之前芯片必须处于 PROVISIONING。0x17 就是这个状态。
set "PROVISIONING_PS_VALUE=0x17"

set "FAILED_STEP="
set "DA_DIR=%~dp0ROT_Provisioning\DA"
set "connect_no_reset=-c port=SWD speed=fast ap=1 mode=Hotplug"
if /i "%TZEN_STATE%"=="y" (set "DA_OBK_NAME=DA_Config.obk") else (set "DA_OBK_NAME=DA_ConfigWithPassword.obk")
set "DA_OBK=%DA_DIR%\Binary\!DA_OBK_NAME!"

echo.
echo ============================================================
echo  只接着写 obk，然后设 CLOSED
echo  不擦除 Flash，不重烧 TF-M
echo  每步等待: %STEP_WAIT_SEC% 秒
echo  obk: !DA_OBK!
echo ============================================================
echo.

if not exist "%~dp0ROT_Provisioning\env.bat" (
    set "FAILED_STEP=找不到 ROT_Provisioning\env.bat"
    goto :fail
)
call "%~dp0ROT_Provisioning\env.bat"
if not exist %stm32programmercli% (
    set "FAILED_STEP=找不到 STM32_Programmer_CLI"
    goto :fail
)
if not exist "!DA_OBK!" (
    echo [FAIL] 没有 !DA_OBK!
    set "FAILED_STEP=缺少 !DA_OBK_NAME!"
    goto :fail
)
echo [ok]  %stm32programmercli%
echo [ok]  已找到 !DA_OBK_NAME!
echo.

rem Hotplug 读不到内核时，改用 Under Reset 按住芯片，避免用户程序挡住调试口。
rem 两种都失败再做一次 DA 证书认证，然后重试。连上的方式记在 connect_no_reset。
set "FAILED_STEP=连接芯片"
call :wait_step "%FAILED_STEP%"
call :pick_connect
if errorlevel 1 goto :fail
echo [ok]  已连上: !connect_no_reset!
echo.

rem 已经是 PROVISIONING 时，CubeProgrammer 会提示未修改并成功返回。
set "FAILED_STEP=确认产品状态 PROVISIONING"
echo CMD: %stm32programmercli% !connect_no_reset! -ob PRODUCT_STATE=%PROVISIONING_PS_VALUE%
%stm32programmercli% !connect_no_reset! -ob PRODUCT_STATE=%PROVISIONING_PS_VALUE%
if errorlevel 1 goto :fail
echo [ok]  产品状态是 PROVISIONING
echo.

rem 下面按连上的方式直接写 obk。不调用 obk_provisioning.bat，它里面写死了 Hotplug。
rem 旧调用留在这里，Hotplug 能连上时可以改回：
rem pushd "%DA_DIR%"
rem cmd /c obk_provisioning.bat %TZEN_STATE% AUTO
rem popd
set "FAILED_STEP=写入 !DA_OBK_NAME!"
call :wait_step "%FAILED_STEP%"
echo CMD: -sdp !DA_OBK_NAME!
echo      目录 %DA_DIR%
pushd "%DA_DIR%"
if errorlevel 1 goto :fail
rem 复位失败不在这里停，接着写 obk。-sdp 失败才算这一步失败。
%stm32programmercli% !connect_no_reset! -hardRst
echo CMD: %stm32programmercli% !connect_no_reset! -sdp .\Binary\!DA_OBK_NAME!
%stm32programmercli% !connect_no_reset! -sdp .\Binary\!DA_OBK_NAME!
set "RC=!ERRORLEVEL!"
popd
if not "!RC!"=="0" goto :fail
echo [ok]  obk 已写入
echo.
rem CLOSED 写完后调试口会断开，CubeProgrammer 报 reconnect 失败是正常现象。
set "FAILED_STEP=产品状态 CLOSED"
call :wait_step "%FAILED_STEP%"
echo 即将写入 CLOSED ^(%FINAL_PS_VALUE%^)。写完调试口会断开，返回码不判断成败。
echo CMD: %stm32programmercli% !connect_no_reset! -ob PRODUCT_STATE=%FINAL_PS_VALUE%
%stm32programmercli% !connect_no_reset! -ob PRODUCT_STATE=%FINAL_PS_VALUE%
echo.
echo ============================================================
echo  obk 已写入，并已发送 CLOSED ^(%FINAL_PS_VALUE%^)。
echo  若最后一行是 failed to reconnect，调试口关掉了，这一步算完成。
echo ============================================================
goto :stay_open


rem 依次试 Hotplug、Under Reset。都不行就做 DA 认证后再试。
rem 成功时把 connect_no_reset 设成能连上的参数，返回 0。
:pick_connect
call :try_connect Hotplug
if not errorlevel 1 exit /b 0
echo [warn] Hotplug 读不到内核，改用 Under Reset。
call :try_connect UR
if not errorlevel 1 exit /b 0
echo [warn] Under Reset 也失败，用叶子证书做一次 Debug Authentication。
pushd "%DA_DIR%"
if errorlevel 1 exit /b 1
echo CMD: debugauth=1 key=Keys/key_3_leaf.pem
%stm32programmercli% -c port=SWD key=Keys/key_3_leaf.pem cert=Certificates/cert_leaf_chain.b64 pwd=Binary/password.bin debugauth=1
set "AUTH_RC=!ERRORLEVEL!"
popd
if not "!AUTH_RC!"=="0" (
    echo [warn] 证书认证没有打开调试口。
)
call :try_connect Hotplug
if not errorlevel 1 exit /b 0
call :try_connect UR
if not errorlevel 1 exit /b 0
echo [FAIL] Hotplug、Under Reset、证书认证之后仍然读不到内核。
echo        先拔掉板子电源和 ST-LINK，再插上，然后重新双击本脚本。
set "FAILED_STEP=连接芯片"
exit /b 1

rem 只试一种连接。%1 是 CubeProgrammer 的 mode：Hotplug 或 UR。
rem 能读到内核就把 connect_no_reset 设好并返回 0。
:try_connect
set "TRY_CONNECT=-c port=SWD speed=fast ap=1 mode=%~1"
echo CMD: %stm32programmercli% !TRY_CONNECT!
%stm32programmercli% !TRY_CONNECT!
if errorlevel 1 exit /b 1
set "connect_no_reset=!TRY_CONNECT!"
exit /b 0
rem 本步开始前等人按键。秒数是 STEP_WAIT_SEC。到点没按键也继续。
:wait_step
echo ------------------------------------------------------------
echo 下一步: %~1
echo 等待 %STEP_WAIT_SEC% 秒。按任意键立即继续。
echo ------------------------------------------------------------
timeout /t %STEP_WAIT_SEC%
echo.
exit /b 0

rem 出错就停。不要继续写 CLOSED，窗口留着。
:fail
echo.
echo ============================================================
echo  失败，已停下: %FAILED_STEP%
echo  没有继续后面的步骤。Flash 没有被本脚本擦除。
echo ============================================================
goto :stay_open

rem 窗口不自动关闭。点右上角才关。
:stay_open
echo.
echo 窗口保持打开，不会自动关闭。
cmd /k
goto :stay_open
