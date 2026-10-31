@echo off
rem Full regression to Open. English only: UTF-8 inside cmd breaks on GBK Windows.
rem Usage: da_regression.bat <private.pem> <cert_chain.b64>
rem CLOSED or PROVISIONED: certificate regression, per=a.
rem PROVISIONING: try the certificate first. If that fails, switch to the example password and regress.
rem Does not restore SECBOOTADD and does not program an image. User flash is erased.
if /i "%~1"=="__RUN__" goto :run
setlocal EnableExtensions
for /f %%I in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set "STAMP=%%I"
set "LOGDIR=%~dp0log"
if not exist "%LOGDIR%" mkdir "%LOGDIR%"
set "LOGFILE=%LOGDIR%\da_regression_%STAMP%.log"
set "LOGBAT=%~f0"
set "LOGARG=%*"
cmd /c ""%LOGBAT%" __RUN__ %LOGARG%" > "%LOGFILE%" 2>&1
set "RC=%ERRORLEVEL%"
echo log: %LOGFILE%
endlocal & exit /b %RC%

:run
shift
setlocal EnableExtensions EnableDelayedExpansion
echo log: da regression
echo.

set "PRIV=%~1"
set "CERT=%~2"
set "PASS=%~3"
if not defined PASS set "PASS=%~dp0password.bin"
if not exist "!PRIV!" (
    echo [FAIL] Private key not found: !PRIV!
    exit /b 1
)
if not exist "!CERT!" (
    echo [FAIL] Certificate chain not found: !CERT!
    exit /b 1
)
echo Private key: !PRIV!
echo Certificate: !CERT!
echo.

call :find_cli
if errorlevel 1 exit /b 1

echo [info] Close debug if it is open
STM32_Programmer_CLI -c port=SWD speed=fast ap=1 mode=Hotplug debugauth=3 >nul 2>&1

call :discover
set "STATE=!PRODUCT!"
echo [info] Product state: !STATE!
if /i "!STATE!"=="OPEN" (
    echo [FAIL] Product state is already Open. Regression is not needed.
    exit /b 1
)
if /i "!STATE!"=="LOCKED" (
    echo [FAIL] Product state is LOCKED. Cannot recover.
    exit /b 1
)
if /i "!STATE!"=="UNKNOWN" (
    echo [FAIL] Product state is unknown.
    exit /b 1
)

echo [info] Auth: !AUTH!
if /i "!AUTH!"=="PASSWORD" goto :password_auth

echo [info] Certificate regression
STM32_Programmer_CLI -c port=SWD per=a key="!PRIV!" cert="!CERT!" debugauth=1
set "CERT_RC=!ERRORLEVEL!"
if "!CERT_RC!"=="0" (
    call :discover
    if /i "!PRODUCT!"=="OPEN" (
        echo [ok] Open
        echo [info] User flash was erased. Program the image again.
        exit /b 0
    )
)
if /i "!STATE!"=="PROVISIONING" goto :password_recover
echo [FAIL] Certificate regression failed. Product state was !STATE!
exit /b 1

:password_auth
echo [info] Device uses ST Password. Pass the password file so CubeProgrammer does not wait for input.
if not defined PASS set "PASS=%~dp0password.bin"
if not exist "!PASS!" (
    echo [FAIL] password.bin not found
    exit /b 1
)
if /i "!STATE!"=="PROVISIONING" goto :password_recover
echo [info] Password regression
STM32_Programmer_CLI -c port=SWD per=a pwd="!PASS!" debugauth=1
if errorlevel 1 (
    echo [FAIL] Password regression failed
    exit /b 1
)
call :discover
if /i "!PRODUCT!"=="OPEN" (
    echo [ok] Open
    echo [info] User flash was erased. Program the image again.
    exit /b 0
)
echo [FAIL] Product state after password regression is !PRODUCT!
exit /b 1

:password_recover
echo [info] PROVISIONING and certificate regression failed. Password recovery.
set "PASS=%~dp0password.bin"
set "PWK=%~dp0DA_ConfigWithPassword.obk"
if not exist "!PASS!" (
    echo [FAIL] password.bin not found
    exit /b 1
)
if not exist "!PWK!" (
    echo [FAIL] DA_ConfigWithPassword.obk not found
    exit /b 1
)
set "RST=-c port=SWD speed=fast ap=1 mode=Hotplug -hardRst"
set "HP=-c port=SWD speed=fast ap=1 mode=Hotplug"
echo [info] Set TZEN=0xC3 and write the password OBK
STM32_Programmer_CLI !RST! -ob TZEN=0xC3
STM32_Programmer_CLI !RST!
STM32_Programmer_CLI !HP! -sdp "!PWK!"
if errorlevel 1 (
    echo [FAIL] Writing password OBK failed
    exit /b 1
)
STM32_Programmer_CLI -c port=SWD per=a key="!PRIV!" cert="!CERT!" pwd="!PASS!" debugauth=1
if errorlevel 1 (
    echo [FAIL] Password regression failed
    exit /b 1
)
call :discover
if /i "!PRODUCT!"=="OPEN" (
    echo [ok] Open
    echo [info] User flash was erased. TrustZone is restored to 0xB4 the next time the release image is programmed.
    exit /b 0
)
echo [FAIL] Product state after password regression is !PRODUCT!
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
set "AUTH=CERTIFICATE"
set "ALINE="
for /f "usebackq delims=" %%L in (`findstr /i /c:"cryptosystems" "%DISC%"`) do set "ALINE=%%L"
if defined ALINE (
    echo !ALINE! | findstr /i /c:"ST Password" >nul
    if not errorlevel 1 set "AUTH=PASSWORD"
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
