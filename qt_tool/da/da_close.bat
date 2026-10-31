@echo off
rem Close STM32H5 with certificate DA. English only: UTF-8 inside cmd breaks on GBK Windows.
rem Usage: da_close.bat <public.pem>
rem Open: enter PROVISIONING, write OBK, then SECBOOTADD + BOOT_UBE + CLOSED.
rem Already PROVISIONING or PROVISIONED: skip 0x17 and finish the close.
rem Does not erase flash and does not call provisioning.bat.
if /i "%~1"=="__RUN__" goto :run
setlocal EnableExtensions
for /f %%I in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set "STAMP=%%I"
set "LOGDIR=%~dp0log"
if not exist "%LOGDIR%" mkdir "%LOGDIR%"
set "LOGFILE=%LOGDIR%\da_close_%STAMP%.log"
set "LOGBAT=%~f0"
set "LOGARG=%*"
cmd /c ""%LOGBAT%" __RUN__ %LOGARG%" > "%LOGFILE%" 2>&1
set "RC=%ERRORLEVEL%"
echo log: %LOGFILE%
endlocal & exit /b %RC%

:run
shift
setlocal EnableExtensions EnableDelayedExpansion
echo log: da close
echo.

set "PUB=%~1"
if not exist "!PUB!" (
    echo [FAIL] Public key not found: !PUB!
    exit /b 1
)
echo Public key: !PUB!
echo.

call :find_cli
if errorlevel 1 exit /b 1

call :discover
echo [info] Product state: !PRODUCT!
echo [info] Auth: !AUTH!
if /i "!PRODUCT!"=="LOCKED" (
    echo [FAIL] Product state is LOCKED. Cannot recover.
    exit /b 1
)
if /i "!PRODUCT!"=="CLOSED" (
    echo [FAIL] Product state is already CLOSED.
    exit /b 1
)
if /i "!PRODUCT!"=="UNKNOWN" (
    echo [FAIL] Product state is unknown.
    exit /b 1
)
if /i not "!AUTH!"=="CERTIFICATE" (
    echo [FAIL] TrustZone is off. Discovery does not report certificate authentication. DA close is not run.
    exit /b 1
)

set "HP=-c port=SWD speed=fast ap=1 mode=Hotplug"
if /i "!PRODUCT!"=="OPEN" (
    echo [info] Product state is Open. Set PRODUCT_STATE=0x17
    STM32_Programmer_CLI !HP! -ob PRODUCT_STATE=0x17
    if errorlevel 1 (
        echo [FAIL] Setting PRODUCT_STATE=0x17 failed
        exit /b 1
    )
) else (
    echo [info] Product state is !PRODUCT!. Continue without writing 0x17
)

set "GEN=%~dp0..\windows-tfm-tools\windows-tfm-tools-h573\windows-tfm-tools\gen_da_obk.py"
set "OBK=%TEMP%\da_close_config.obk"
if not exist "!GEN!" (
    echo [FAIL] Cannot find gen_da_obk.py
    exit /b 1
)
echo [info] Generate OBK
where py >nul 2>&1
if not errorlevel 1 (
    py -3 "!GEN!" -InFile "!PUB!" -OutFile "!OBK!" -Permission 0x00005077 -Encrypt 1
) else (
    python "!GEN!" -InFile "!PUB!" -OutFile "!OBK!" -Permission 0x00005077 -Encrypt 1
)
if errorlevel 1 (
    echo [FAIL] Generating OBK failed
    exit /b 1
)
if not exist "!OBK!" (
    echo [FAIL] OBK was not created
    exit /b 1
)

echo [info] Write OBK
STM32_Programmer_CLI !HP! -sdp "!OBK!"
if errorlevel 1 (
    echo [FAIL] Writing OBK failed
    exit /b 1
)

echo [info] SECBOOTADD=0xC0100 BOOT_UBE=0xB4 PRODUCT_STATE=0x72
STM32_Programmer_CLI !HP! -ob SECBOOTADD=0xC0100 BOOT_UBE=0xB4 PRODUCT_STATE=0x72
echo [info] The debug port drops when the product state becomes CLOSED. The command result is ignored.
call :discover
if /i "!PRODUCT!"=="CLOSED" (
    echo [ok] CLOSED
    exit /b 0
)
echo [FAIL] Product state after close is !PRODUCT!
exit /b 1

:find_cli
echo [1] Locate STM32_Programmer_CLI
set "CUBEPROG="
if exist "D:\ST\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" (
    set "CUBEPROG=D:\ST\STM32CubeProgrammer\bin"
)
if not defined CUBEPROG if exist "%ProgramFiles%\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" (
    set "CUBEPROG=%ProgramFiles%\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin"
)
if not defined CUBEPROG if exist "%ProgramFiles(x86)%\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" (
    set "CUBEPROG=%ProgramFiles(x86)%\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin"
)
if defined CUBEPROG set "PATH=%CUBEPROG%;%PATH%"
where STM32_Programmer_CLI >nul 2>&1
if errorlevel 1 (
    echo [FAIL] STM32_Programmer_CLI not found
    exit /b 1
)
echo [ok]   STM32_Programmer_CLI
echo.
exit /b 0

:discover
set "PRODUCT=UNKNOWN"
set "DISC=%TEMP%\da_product_state.txt"
echo [info] Discovery
STM32_Programmer_CLI -c port=SWD debugauth=2 > "%DISC%" 2>&1
type "%DISC%"
set "AUTH=UNKNOWN"
set "ALINE="
for /f "usebackq delims=" %%L in (`findstr /i /c:"cryptosystems" "%DISC%"`) do set "ALINE=%%L"
if defined ALINE (
    echo !ALINE! | findstr /i /c:"ST Password" >nul
    if not errorlevel 1 set "AUTH=PASSWORD"
    echo !ALINE! | findstr /i /c:"Certificate" /c:"ECDSA" >nul
    if not errorlevel 1 set "AUTH=CERTIFICATE"
)
set "PLINE="
for /f "usebackq delims=" %%L in (`findstr /i /c:"Product state" /c:"PSA lifecycle" "%DISC%"`) do set "PLINE=%%L"
if not defined PLINE exit /b 0
echo !PLINE! | findstr /i /c:"LOCKED" /c:"0x5C" >nul
if not errorlevel 1 (
    set "PRODUCT=LOCKED"
    exit /b 0
)
echo !PLINE! | findstr /i /c:"PROVISIONING" /c:"0x17" >nul
if not errorlevel 1 (
    set "PRODUCT=PROVISIONING"
    exit /b 0
)
echo !PLINE! | findstr /i /c:"TZ_CLOSED" /c:"TZ-Closed" /c:"TZ Closed" /c:"0xC6" >nul
if not errorlevel 1 (
    set "PRODUCT=CLOSED"
    exit /b 0
)
echo !PLINE! | findstr /i /c:"CLOSED" /c:"0x72" >nul
if not errorlevel 1 (
    set "PRODUCT=CLOSED"
    exit /b 0
)
echo !PLINE! | findstr /i /c:"PROVISIONED" /c:"0x2E" >nul
if not errorlevel 1 (
    set "PRODUCT=PROVISIONED"
    exit /b 0
)
echo !PLINE! | findstr /i /c:"OPEN" /c:"0xED" >nul
if not errorlevel 1 (
    set "PRODUCT=OPEN"
    exit /b 0
)
exit /b 0
