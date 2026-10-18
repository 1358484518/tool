@echo off
rem Run STM32_Programmer_CLI using the caller's CLI_ARGS environment variable.
rem
rem Do not pass "-c port=SWD -ob PRODUCT_STATE=0xED" as subroutine arguments.
rem cmd.exe treats "=" as an argument delimiter, so those become
rem   -c port SWD -ob PRODUCT_STATE 0xED
rem and CubeProgrammer rejects the option bytes. tfm_update.bat used to
rem fail in regression.bat for this reason. Keep the full command in CLI_ARGS.
if not defined CLI_ARGS (
    echo [FAIL] CLI_ARGS is empty
    exit /b 1
)
echo ------------------------------------------------------------
if defined STEP_ID (
    echo STEP %STEP_ID%  %STEP_NAME%
)
echo CMD: STM32_Programmer_CLI %CLI_ARGS%
echo ------------------------------------------------------------
STM32_Programmer_CLI %CLI_ARGS%
if errorlevel 1 (
    echo.
    echo [FAIL] step %STEP_ID% : %STEP_NAME%
    echo        command: STM32_Programmer_CLI %CLI_ARGS%
    exit /b 1
)
echo [ok]   step %STEP_ID% done
exit /b 0
