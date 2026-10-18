@echo off
rem ****************************************************************************
rem  STM32H573 一条龙：DA 初始选项字节 -> TF-M 烧录 -> 写入 DA obk -> CLOSED
rem
rem  顺序与手工流程一致，共 3 段：
rem    1. provisioning 跑到“下载程序”之前：da_close_ob_programming.bat
rem       只写 DA 初始选项字节（SECBOOTADD=0xC0000），不烧 TF-M
rem    2. da_close_tfm_update.bat  改成 TF-M 选项字节（SECBOOTADD=0xC0100）并烧录
rem    3. 回到 provisioning：PROVISIONING、写入 obk、产品状态 CLOSED
rem
rem  任一步失败就停，不再往下做。窗口不会自己关。
rem  用法：
rem    da_tfm_close.bat
rem    da_tfm_close.bat <ST-LINK 序列号>
rem ****************************************************************************
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"
title STM32H573 DA + TF-M CLOSE

rem ============================================================================
rem  STEP_WAIT_SEC
rem  每个步骤开始前等待按键的秒数。只改下面这一行。
rem  按任意键立刻开始这一步；秒数走完仍然没按键，也会开始这一步。
rem  合法范围 1 到 99999。出错时不走这个等待，直接停下。
rem ============================================================================
set "STEP_WAIT_SEC=30"

rem TZEN_STATE：TrustZone。y=打开（默认，选项字节 0xB4），n=关闭。
rem y 使用 Binary\DA_Config.obk；n 使用 Binary\DA_ConfigWithPassword.obk。
set "TZEN_STATE=y"

rem 最后写入的产品状态。CLOSED=0x72。TZ-CLOSED 才是 0xC6，不要和这里混。
set "FINAL_PS_NAME=CLOSED"
set "FINAL_PS_VALUE=0x72"

rem PROVISIONING 的选项字节值。写 obk 之前必须先进入这个状态。
set "PROVISIONING_PS_VALUE=0x17"

set "FAILED_STEP="
set "SN_ARG="
if not "%~1"=="" set "SN_ARG=%~1"

rem DA 脚本所在目录。ob_programming / obk_provisioning 依赖这个目录当当前目录。
set "DA_DIR=%~dp0ROT_Provisioning\DA"
rem 和 provisioning.bat 一样的连接参数：SWD、不复位、AP1。
set "connect_no_reset=-c port=SWD speed=fast ap=1 mode=Hotplug"

if /i "%TZEN_STATE%"=="y" (
    set "DA_OBK_NAME=DA_Config.obk"
) else (
    set "DA_OBK_NAME=DA_ConfigWithPassword.obk"
)
set "DA_OBK=%DA_DIR%\Binary\!DA_OBK_NAME!"

echo.
echo ============================================================
echo  STM32H573  DA 预置 + TF-M 烧录 + %FINAL_PS_NAME%
echo  每步等待: %STEP_WAIT_SEC% 秒（改脚本开头的 STEP_WAIT_SEC）
echo  TrustZone: %TZEN_STATE%
echo  obk: !DA_OBK!
echo ============================================================
echo.

rem 工具路径在 ROT_Provisioning\env.bat，和 DA 脚本用的是同一份。
if not exist "%~dp0ROT_Provisioning\env.bat" (
    set "FAILED_STEP=找不到 ROT_Provisioning\env.bat"
    goto :fail
)
call "%~dp0ROT_Provisioning\env.bat"
if not exist %stm32programmercli% (
    set "FAILED_STEP=找不到 STM32_Programmer_CLI"
    goto :fail
)
echo [ok]  %stm32programmercli%
echo.

rem obk 要事先用 TrustedPackageCreator 按 DA_Config.xml 生成。这里不生成。
if not exist "!DA_OBK!" (
    echo [FAIL] 没有 !DA_OBK!
    echo        用 TrustedPackageCreator 打开
    echo        %DA_DIR%\Config\DA_Config.xml
    echo        生成 !DA_OBK_NAME! 到 %DA_DIR%\Binary\ 后再运行本脚本。
    set "FAILED_STEP=缺少 !DA_OBK_NAME!"
    goto :fail
)
echo [ok]  已找到 !DA_OBK_NAME!
echo.

rem ----- 1/3 对应 provisioning.bat 在“请下载程序”之前。不烧镜像。
set "FAILED_STEP=1/3 DA初始选项字节（下载程序之前）"
call :wait_step "%FAILED_STEP%"
call :run_da_script "%~dp0da_close_ob_programming.bat" %TZEN_STATE%
if errorlevel 1 goto :fail
echo [ok]  DA 初始选项字节写完。下一步才烧 TF-M。
echo.

rem ----- 2/3 TF-M。没成功就不要进入 PROVISIONING，更不要写 CLOSED。
set "FAILED_STEP=2/3 da_close_tfm_update 修改选项字节并烧录"
call :wait_step "%FAILED_STEP%"
rem 告诉 da_close_regression.bat 和 da_close_tfm_update.bat：结尾别 pause，失败码交回这里。
set "TFM_OUTER_NO_PAUSE=1"
if defined SN_ARG (
    call "%~dp0da_close_tfm_update.bat" %SN_ARG%
) else (
    call "%~dp0da_close_tfm_update.bat"
)
set "RC=!ERRORLEVEL!"
set "TFM_OUTER_NO_PAUSE="
if not "!RC!"=="0" goto :fail
echo [ok]  da_close_tfm_update 完成，没有错误
echo.

rem ----- 3/3 回到 provisioning：先 PROVISIONING，再写 obk，最后 CLOSED。
set "FAILED_STEP=3/3 产品状态 PROVISIONING"
call :wait_step "%FAILED_STEP%"
echo CMD: %stm32programmercli% %connect_no_reset% -ob PRODUCT_STATE=%PROVISIONING_PS_VALUE%
%stm32programmercli% %connect_no_reset% -ob PRODUCT_STATE=%PROVISIONING_PS_VALUE%
if errorlevel 1 goto :fail
echo [ok]  PRODUCT_STATE=%PROVISIONING_PS_VALUE%
echo.

rem ----- 4/5 把 DA 证书配置写进芯片。
set "FAILED_STEP=3/3 obk_provisioning 写入 !DA_OBK_NAME!"
call :wait_step "%FAILED_STEP%"
call :run_da_script obk_provisioning.bat %TZEN_STATE%
if errorlevel 1 goto :fail
echo [ok]  obk 已写入
echo.

rem ----- 5/5 CLOSED。写完后调试口会断，CubeProgrammer 的返回码不能当作失败。
set "FAILED_STEP=3/3 产品状态 %FINAL_PS_NAME%"
call :wait_step "%FAILED_STEP%"
echo 即将写入 %FINAL_PS_NAME% ^(%FINAL_PS_VALUE%^)。写完调试口会断开，返回码不判断成败。
echo CMD: %stm32programmercli% %connect_no_reset% -ob PRODUCT_STATE=%FINAL_PS_VALUE%
%stm32programmercli% %connect_no_reset% -ob PRODUCT_STATE=%FINAL_PS_VALUE%
echo.
echo ============================================================
echo  三段都已做完。最后一步是 %FINAL_PS_NAME% ^(%FINAL_PS_VALUE%^)。
echo ============================================================
goto :stay_open

rem 本步开始前等人按键。秒数是 STEP_WAIT_SEC。
rem 按任意键马上继续；时间到了没按键也继续。本函数不表示失败。
:wait_step
echo ------------------------------------------------------------
echo 下一步: %~1
echo 等待 %STEP_WAIT_SEC% 秒。按任意键立即继续。
echo ------------------------------------------------------------
timeout /t %STEP_WAIT_SEC%
echo.
exit /b 0

rem 在 DA 目录用子进程跑选项字节或 obk 脚本。
rem 这些脚本成功时是 exit，不是 exit /b。直接 call 会把本窗口一起关掉。
rem %1 脚本全路径或文件名，%2 TrustZone：y 或 n。第二个参数固定传 AUTO。
:run_da_script
echo CMD: %~1 %~2 AUTO
echo      目录 %DA_DIR%
pushd "%DA_DIR%"
if errorlevel 1 exit /b 1
cmd /c "%~1" %~2 AUTO
set "RC=!ERRORLEVEL!"
popd
if not "!RC!"=="0" exit /b 1
exit /b 0

rem 中间出错。后面的步骤不再执行，窗口留着。
:fail
echo.
echo ============================================================
echo  失败，已停下: %FAILED_STEP%
echo  后面的步骤没有执行。
echo ============================================================
goto :stay_open

rem 不让双击打开的窗口自己关掉。点窗口右上角才能关。
rem 在这里输入 exit 只会再打开一层，外层脚本还在。
:stay_open
echo.
echo 窗口保持打开，不会自动关闭。
cmd /k
goto :stay_open
