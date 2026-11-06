@echo off
rem This file MUST use Windows CRLF line endings (LF-only breaks cmd.exe).
echo === Starting signing process ===
rem Standalone MCUboot signer. Pass --chip h573 or --chip h5f4.
rem h573: layout\h573, S 512 KB @ 0x0C044000, NS 1 MB @ 0x0C100000.
rem h5f4: layout\h5f4, S 512 KB @ 0x0C078000, NS 1024 KB @ 0x0C0F8000.
rem Three signing modes:
rem   local  (default)        : sign with the development key in keys\dev\<key_id>.pem - no token, no network
rem   remote (--remote)       : sign on the internal signing server; only public keys are kept locally
rem   prod   (sign_prod.bat)  : release images - forces the server and refuses --local
rem
rem Usage:
rem   sign.bat tfm_ns.bin                      infer the kind from the file name, use the default profile
rem   sign.bat --chip h573 --profile DEV-CCU sapp.bin
rem   sign.bat --key-id OCPP-FW-CCU app.bin    use this key_id explicitly
rem   sign.bat --dev sapp.bin                  force local signing with the development key
rem   sign.bat --local sapp.bin                same as --dev (kept for compatibility)
rem   sign.bat --remote sapp.bin               force signing on the internal server
rem   sign.bat ns app.bin                      force the non-secure side
rem SPDX-License-Identifier: BSD-3-Clause

setlocal EnableExtensions EnableDelayedExpansion

set "KIT=%~dp0"
if "%KIT:~-1%"=="\" set "KIT=%KIT:~0,-1%"

set "CONF_FILE=%KIT%\remote.conf"
if defined SIGN_CONF set "CONF_FILE=%SIGN_CONF%"

set "KIND="
set "IN_NAME="
set "PROFILE="
set "KEY_ID_OVERRIDE="
set "MODE_OVERRIDE="
set "PROD="
set "CHIP="
set "VERSION_OVERRIDE="

if "%~1"=="" goto :usage_fail

:parse_args
if "%~1"=="" goto :args_done
if /I "%~1"=="-h" goto :usage_ok
if /I "%~1"=="--help" goto :usage_ok
if /I "%~1"=="/?" goto :usage_ok
if /I "%~1"=="--profile" set "PROFILE=%~2" & shift & shift & goto :parse_args
if /I "%~1"=="-p" set "PROFILE=%~2" & shift & shift & goto :parse_args
if /I "%~1"=="--key-id" set "KEY_ID_OVERRIDE=%~2" & shift & shift & goto :parse_args
if /I "%~1"=="--conf" set "CONF_FILE=%~2" & shift & shift & goto :parse_args
if /I "%~1"=="--remote" set "MODE_OVERRIDE=remote" & shift & goto :parse_args
if /I "%~1"=="--local" set "MODE_OVERRIDE=local" & shift & goto :parse_args
if /I "%~1"=="--dev" set "MODE_OVERRIDE=local" & shift & goto :parse_args
if /I "%~1"=="--prod" set "PROD=1" & set "MODE_OVERRIDE=remote" & shift & goto :parse_args
if /I "%~1"=="--chip" set "CHIP=%~2" & shift & shift & goto :parse_args
if /I "%~1"=="--version" set "VERSION_OVERRIDE=%~2" & shift & shift & goto :parse_args
if /I "%~1"=="ns" set "KIND=ns" & shift & goto :parse_args
if /I "%~1"=="nspe" set "KIND=ns" & shift & goto :parse_args
if /I "%~1"=="s" set "KIND=s" & shift & goto :parse_args
if /I "%~1"=="sapp" set "KIND=s" & shift & goto :parse_args
if /I "%~1"=="spe" set "KIND=s" & shift & goto :parse_args
set "IN_NAME=%~1"
shift
if not "%~1"=="" (
    echo ERROR: too many arguments: %1
    exit /b 2
)

:args_done
if /I "%CHIP%"=="h573" goto :chip_ok
if /I "%CHIP%"=="h5f4" goto :chip_ok
echo ERROR: pass --chip h573 or --chip h5f4
exit /b 2
:chip_ok
if /I "%CHIP%"=="h5f4" goto :slot_h5f4
set "SLOT_S=S   512KB @ 0x0C044000"
set "SLOT_NS=NS  1024KB @ 0x0C100000"
goto :slot_done
:slot_h5f4
set "SLOT_S=S   512KB @ 0x0C078000"
set "SLOT_NS=NS  1024KB @ 0x0C0F8000"
:slot_done
set "CONFIG_FILE=%KIT%\config.%CHIP%"
call :load_config
if errorlevel 1 exit /b 1
if "%IN_NAME%"=="" goto :usage_fail

set "IN_BIN="
if exist "%IN_NAME%" (
    for %%I in ("%IN_NAME%") do set "IN_BIN=%%~fI"
) else if exist "%KIT%\%IN_NAME%" (
    set "IN_BIN=%KIT%\%IN_NAME%"
) else (
    echo ERROR: file not found: %IN_NAME%
    echo Put it in "%KIT%" or give a full path.
    exit /b 1
)

for %%I in ("%IN_BIN%") do (
    set "BASE=%%~nxI"
    set "STEM=%%~nI"
    set "IN_DIR=%%~dpI"
)
if "%IN_DIR:~-1%"=="\" set "IN_DIR=%IN_DIR:~0,-1%"

if not "%KIND%"=="" goto :kind_ready
echo %BASE%| findstr /I /C:"ns" >nul
if not errorlevel 1 (
    set "KIND=ns"
    goto :kind_ready
)
echo %BASE%| findstr /I /C:"sapp" /C:"tfm_s" /C:"_s.bin" /C:"_s_" >nul
if not errorlevel 1 (
    set "KIND=s"
    goto :kind_ready
)
echo ERROR: cannot tell NS from S by the file name
echo Use: sign.bat ns %BASE%
echo Or:  sign.bat s %BASE%
exit /b 1

:kind_ready
rem Output directory: next to the input by default; OUT_DIR=kit writes into this sign_kit folder
call :conf_get "OUT_DIR"
if /I "%CONF_VAL%"=="kit" (
    set "OUT_BIN=%KIT%\%STEM%_signed.bin"
) else (
    set "OUT_BIN=%IN_DIR%\%STEM%_signed.bin"
)

rem -------------------------------------------------------------------------
rem Signing mode and key_id
rem -------------------------------------------------------------------------
set "MODE="
if defined MODE_OVERRIDE set "MODE=%MODE_OVERRIDE%"
rem Note: do not put the CONF_VAL lookup inside a parenthesised block - CONF_VAL is expanded when
rem       the block is parsed there, so it would keep the stale value of the previous conf_get
rem       (same trap as the PROFILE lookup below). Hence the goto.
if defined MODE goto :mode_ready
call :conf_get "SIGN_MODE"
if defined CONF_VAL set "MODE=%CONF_VAL%"
:mode_ready
if not defined MODE if exist "%CONF_FILE%" set "MODE=remote"
if not defined MODE set "MODE=local"

rem sign_prod.bat passes --prod: production keys live on the signing server, so local signing is refused
if defined PROD if /I "%MODE%"=="local" (
    echo ERROR: --prod always signs on the internal server and refuses --local
    echo        Production keys never leave the server; for a development image drop both flags.
    exit /b 2
)

set "KEY_ID="
set "KEY_ARGS="
if /I not "%MODE%"=="remote" goto :local_mode

if not exist "%CONF_FILE%" (
    echo ERROR: SIGN_MODE=remote but the config file was not found: "%CONF_FILE%"
    echo The file ships with the repo and holds the shared key_id map; restore it from version control if missing.
    exit /b 1
)

call :conf_get "SIGN_SERVER_URL"
set "SERVER_URL=%CONF_VAL%"
if not defined SERVER_URL set "SERVER_URL=%TFM_SIGN_SERVER_URL%"
if not defined SERVER_URL (
    echo ERROR: signing server URL is not configured
    echo Provide it in one of these ways - the first one is recommended, per developer:
    echo   set TFM_SIGN_SERVER_URL=https://^<server-host^>:8443
    echo   or put SIGN_SERVER_URL=https://^<server-host^>:8443 into "%CONF_FILE%"
    exit /b 1
)

if defined KEY_ID_OVERRIDE (
    set "KEY_ID=%KEY_ID_OVERRIDE%"
    goto :remote_key_ready
)

rem Note: do NOT combine the "if not defined PROFILE" test and the DEFAULT_PROFILE lookup into
rem       one block: CONF_VAL inside a block expands when the block is parsed, so it would yield
rem       the stale value from the previous conf_get. Hence the goto below.
if defined PROFILE goto :profile_ready
if defined PROD goto :prod_profile
call :conf_get "DEFAULT_PROFILE"
if defined CONF_VAL set "PROFILE=%CONF_VAL%"
goto :profile_ready
:prod_profile
rem Production profile: PROD_PROFILE in remote.conf, so the daily DEFAULT_PROFILE stays untouched
call :conf_get "PROD_PROFILE"
if defined CONF_VAL set "PROFILE=%CONF_VAL%"
if not defined PROFILE (
    echo ERROR: production signing but PROD_PROFILE is not configured in "%CONF_FILE%"
    echo Add PROD_PROFILE=^<production-profile^> there, or pass --profile explicitly.
    exit /b 1
)
:profile_ready
if not defined PROFILE (
    echo ERROR: no profile given
    echo Use --profile NAME, or set DEFAULT_PROFILE in "%CONF_FILE%", or pass --key-id.
    exit /b 1
)

if /I "%KIND%"=="ns" (set "MAP_KEY=PROFILE_%PROFILE%_NS") else (set "MAP_KEY=PROFILE_%PROFILE%_SPE")
call :conf_get "%MAP_KEY%"
if not defined CONF_VAL (
    echo ERROR: profile "%PROFILE%" has no key_id mapping for side %KIND%
    echo Add this to "%CONF_FILE%": %MAP_KEY%=^<key_id^>
    echo Note: key_id is never inferred from "<name>-SPE" - this avoids signing with the wrong key.
    exit /b 1
)
set "KEY_ID=%CONF_VAL%"

:remote_key_ready
set "PUBKEY=%KIT%\keys\%KEY_ID%.pem"
if not exist "%PUBKEY%" (
    echo ERROR: local public key for key_id "%KEY_ID%" is missing: "%PUBKEY%"
    echo Export it and commit it to the repo; it is used for a strict fingerprint check before signing:
    echo   py -3 "%KIT%\scripts\fetch_pubkeys.py" --key-id %KEY_ID%
    exit /b 1
)
set KEY_ARGS=--key "%PUBKEY%" --remote-sign "%CONF_FILE%" --key-id "%KEY_ID%"
set "MODE_UP=remote"
set "KEY_HINT=%PUBKEY%"

rem Token pre-check (friendlier than a Python traceback)
call :conf_get "TOKEN_ENV"
set "TOKEN_ENV_NAME=%CONF_VAL%"
if not defined TOKEN_ENV_NAME set "TOKEN_ENV_NAME=TFM_SIGN_TOKEN"
call set "TOKEN_VALUE=%%%TOKEN_ENV_NAME%%%"
if defined TOKEN_VALUE goto :token_ready
call :conf_get "TOKEN_FILE"
set "TOKEN_FILE_PATH=%CONF_VAL%"
if not defined TOKEN_FILE_PATH set "TOKEN_FILE_PATH=%USERPROFILE%\.tfm_sign_token"
if "%TOKEN_FILE_PATH:~0,1%"=="~" set "TOKEN_FILE_PATH=%USERPROFILE%%TOKEN_FILE_PATH:~1%"
if not exist "%TOKEN_FILE_PATH%" (
    echo ERROR: signing token not found
    echo Provide it in one of these ways - never commit the token:
    echo   set %TOKEN_ENV_NAME%=^<token^>                    ^(current session^)
    echo   or write the token to "%TOKEN_FILE_PATH%"
    exit /b 1
)
goto :token_ready

:local_mode
rem Resolve the key_id exactly like remote mode does: --key-id, else the profile map.
rem It selects which development private key in keys\dev\ is used.
if defined KEY_ID_OVERRIDE set "KEY_ID=%KEY_ID_OVERRIDE%" & goto :local_key_ready
if defined PROFILE goto :local_profile_ready
call :conf_get "DEFAULT_PROFILE"
if defined CONF_VAL set "PROFILE=%CONF_VAL%"
:local_profile_ready
if not defined PROFILE (
    echo ERROR: no profile given
    echo Use --profile NAME, or set DEFAULT_PROFILE in "%CONF_FILE%", or pass --key-id.
    exit /b 1
)
if /I "%KIND%"=="ns" (set "MAP_KEY=PROFILE_%PROFILE%_NS") else (set "MAP_KEY=PROFILE_%PROFILE%_SPE")
call :conf_get "%MAP_KEY%"
if not defined CONF_VAL (
    echo ERROR: profile "%PROFILE%" has no key_id mapping for side %KIND%
    echo Add this to "%CONF_FILE%": %MAP_KEY%=^<key_id^>
    exit /b 1
)
set "KEY_ID=%CONF_VAL%"

:local_key_ready
rem Development keys are committed under keys\dev\; the old flat file name is kept as a fallback.
set "PRIV_KEY=%KIT%\keys\dev\%KEY_ID%.pem"
if not exist "%PRIV_KEY%" set "PRIV_KEY=%KIT%\keys\image_%KIND%_signing_private_key.pem"
if not exist "%PRIV_KEY%" (
    echo ERROR: development private key not found for key_id "%KEY_ID%"
    echo   expected: "%KIT%\keys\dev\%KEY_ID%.pem"
    echo   fallback: "%KIT%\keys\image_%KIND%_signing_private_key.pem"
    echo Production keys never leave the server - for a release image run sign_prod.bat.
    exit /b 1
)
echo NOTE: local development signing - key_id "%KEY_ID%", no token and no network needed>&2
set KEY_ARGS=--key "%PRIV_KEY%"
set "MODE_UP=local"
set "KEY_HINT=%PRIV_KEY%"

:token_ready

set "LAYOUT_DIR=%KIT%\layout\%CHIP%"
if /I "%KIND%"=="ns" (
    set "LAYOUT=%LAYOUT_DIR%\signing_layout_ns.o"
    set "VERSION=%MCUBOOT_IMAGE_VERSION_NS%"
    set "SEC_CNT=%MCUBOOT_SECURITY_COUNTER_NS%"
    set "DEP=(0, %MCUBOOT_S_IMAGE_MIN_VER%)"
    set "SLOT_HINT=%SLOT_NS%"
    set "KIND_UP=NS"
) else (
    set "LAYOUT=%LAYOUT_DIR%\signing_layout_s.o"
    set "VERSION=%MCUBOOT_IMAGE_VERSION_S%"
    set "SEC_CNT=%MCUBOOT_SECURITY_COUNTER_S%"
    set "DEP=(1, %MCUBOOT_NS_IMAGE_MIN_VER%)"
    set "SLOT_HINT=%SLOT_S%"
    set "KIND_UP=S"
)
if defined VERSION_OVERRIDE set "VERSION=%VERSION_OVERRIDE%"

if not exist "%LAYOUT%" echo ERROR: missing signing file: %LAYOUT% & exit /b 1
if not exist "%KIT%\scripts\wrapper.py" echo ERROR: missing signing file: %KIT%\scripts\wrapper.py & exit /b 1
if not exist "%KIT%\scripts\remote_sign.py" echo ERROR: missing signing file: %KIT%\scripts\remote_sign.py & exit /b 1
if not exist "%KIT%\bl2\macro_parser.py" echo ERROR: missing signing file: %KIT%\bl2\macro_parser.py & exit /b 1

call :find_python
if errorlevel 1 exit /b 1

"%PY%" %PY_EXTRA% -c "import click, cryptography, cbor2, intelhex" 2>nul
if errorlevel 1 (
    call :make_venv
    if errorlevel 1 exit /b 1
)

if /I "%MCUBOOT_HW_KEY%"=="ON" (set "PUB_FMT=full") else (set "PUB_FMT=hash")

set "EXTRA="
if /I "%MCUBOOT_UPGRADE_STRATEGY%"=="OVERWRITE_ONLY" set "EXTRA=!EXTRA! --overwrite-only"
if /I "%MCUBOOT_CONFIRM_IMAGE%"=="ON" set "EXTRA=!EXTRA! --confirm"
if /I "%MCUBOOT_MEASURED_BOOT%"=="ON" set "EXTRA=!EXTRA! --measured-boot-record"
if /I "%MCUBOOT_ENC_IMAGES%"=="ON" (
    set "ENC_KEY=%KIT%\keys\image_enc_%KIND%_key.pem"
    if not exist "!ENC_KEY!" (
        echo ERROR: encryption is enabled but the key was not found: !ENC_KEY!
        exit /b 1
    )
    set "EXTRA=!EXTRA! -E "!ENC_KEY!""
)

rem Keep this echo OUTSIDE any ( ) block: parentheses in the text break cmd block parsing.
set "MODE_LABEL=%MODE_UP%"
if defined PROD set "MODE_LABEL=PRODUCTION / remote"
echo Signing %KIND_UP% image  (%SLOT_HINT%)  [%MODE_LABEL%]
if /I "%MODE%"=="remote" (
    echo   server   %SERVER_URL%
    echo   key_id  %KEY_ID%
    if defined PROFILE echo   profile  %PROFILE%
    if defined PROD echo   note     production key stays on the server - keep the token secret
)
echo   key      %KEY_HINT%
echo   input    %IN_BIN%
echo   output   %OUT_BIN%
echo   version  %VERSION%  header %BL2_HEADER_SIZE%  align %MCUBOOT_ALIGN_VAL%  sec_cnt %SEC_CNT%
echo   python %PY% %PY_EXTRA%

rem cwd must be scripts\ so that wrapper.py prefers the bundled imgtool
pushd "%KIT%\scripts" || exit /b 1
set "PYTHONPATH=%KIT%;%PYTHONPATH%"
"%PY%" %PY_EXTRA% "%KIT%\scripts\wrapper.py" ^
    --version "%VERSION%" ^
    --layout "%LAYOUT%" ^
    --public-key-format "%PUB_FMT%" ^
    --align "%MCUBOOT_ALIGN_VAL%" ^
    --pad ^
    --pad-header ^
    -H "%BL2_HEADER_SIZE%" ^
    -s "%SEC_CNT%" ^
    -L "%MCUBOOT_ENC_KEY_LEN%" ^
    -d "%DEP%" ^
    %KEY_ARGS% ^
    %EXTRA% ^
    "%IN_BIN%" ^
    "%OUT_BIN%"
set "ERR=%ERRORLEVEL%"
popd
if not "%ERR%"=="0" (
    echo ERROR: signing failed, exit code %ERR%
    exit /b %ERR%
)

echo DONE: %OUT_BIN%
exit /b 0

:usage_ok
call :usage
exit /b 0

:usage_fail
call :usage
exit /b 2

:usage
echo Put the unsigned S / NS firmware in this directory and run:
echo.
echo   sign.bat [options] FILENAME
echo.
echo Options:
echo   --profile NAME select the profile; SPE / NS keys come from PROFILE_NAME_SPE / _NS in remote.conf
echo   --key-id ID     use this key_id for this run, overriding the profile map
echo   --dev           local signing with the development key keys\dev\^<key_id^>.pem (default)
echo   --local         same as --dev (kept for compatibility)
echo   --remote        sign on the internal signing server instead
echo   --prod          production signing: forces the server and refuses --local (see sign_prod.bat)
echo   --chip h573^|h5f4  select the slot layout and config.h573 / config.h5f4
echo   --version X.Y.Z    image version for this file. Omit to use the config file
echo   --conf PATH     use the given remote.conf
echo.
echo Examples:
echo   sign.bat tfm_ns.bin
echo   sign.bat --chip h573 --profile DEV-CCU sapp.bin
echo   sign.bat --key-id OCPP-FW-CCU app.bin
echo   sign.bat --remote sapp.bin
echo   sign.bat ns  app.bin
echo.
echo Output: FILENAME_signed.bin next to the input file (OUT_DIR=kit writes into this kit folder)
echo.
echo Local signing uses the development key only: no token and no network.
echo Release images: run sign_prod.bat - the production key stays on the server and a token is required:
echo   set TFM_SIGN_TOKEN=^<token^>
echo   or write the token to %USERPROFILE%\.tfm_sign_token
exit /b 0

:load_config
if not defined CONFIG_FILE set "CONFIG_FILE=%KIT%\config.%CHIP%"
if not exist "%CONFIG_FILE%" (
    echo ERROR: config file not found: %CONFIG_FILE%
    exit /b 1
)
rem config is UTF-8. Read the ASCII key=value copy, not the original file.
call :write_ascii_conf "%CONFIG_FILE%" "%TEMP%\tfm_sign_config.txt"
if errorlevel 1 exit /b 1
for /f "usebackq eol=# tokens=1,* delims==" %%A in ("%TEMP%\tfm_sign_config.txt") do (
    if not "%%A"=="" set "%%A=%%B"
)
del /f /q "%TEMP%\tfm_sign_config.txt" >nul 2>&1
exit /b 0

rem Read one key from remote.conf into CONF_VAL.
rem Names contain '-'; do not turn the whole file into variables.
:conf_get
set "CONF_VAL="
if not exist "%CONF_FILE%" exit /b 1
set "CONF_KEY=%~1"
call :write_ascii_conf "%CONF_FILE%" "%TEMP%\tfm_sign_conf.txt"
if errorlevel 1 exit /b 1
for /f "usebackq eol=# tokens=1,* delims==" %%A in ("%TEMP%\tfm_sign_conf.txt") do (
    if /I "%%A"=="%CONF_KEY%" set "CONF_VAL=%%B"
)
del /f /q "%TEMP%\tfm_sign_conf.txt" >nul 2>&1
exit /b 0

rem Copy a UTF-8 KEY=VALUE file to ASCII lines. Drop comments.
rem cmd for /f uses GBK and can glue the next line onto a Chinese comment.
:write_ascii_conf
set "CONF_SRC=%~1"
set "CONF_DST=%~2"
if exist "%CONF_DST%" del /f /q "%CONF_DST%" >nul 2>&1
powershell -NoProfile -Command "$p=$env:CONF_SRC; $o=$env:CONF_DST; $lines=New-Object System.Collections.Generic.List[string]; foreach($line in [IO.File]::ReadAllLines($p)){ $t=$line.Trim(); if($t.Length -eq 0){ continue }; if($t[0] -eq [char]35){ continue }; $i=$t.IndexOf([char]61); if($i -lt 1){ continue }; $name=$t.Substring(0,$i).Trim(); $v=$t.Substring($i+1).Trim(); $h=$v.IndexOf([char]35); if($h -ge 0){ $v=$v.Substring(0,$h).Trim() }; $lines.Add($name + [char]61 + $v) }; $enc=New-Object System.Text.ASCIIEncoding; [IO.File]::WriteAllLines($o, $lines.ToArray(), $enc)"
if errorlevel 1 (
    echo ERROR: failed to read "%CONF_SRC%"
    exit /b 1
)
if not exist "%CONF_DST%" (
    echo ERROR: failed to read "%CONF_SRC%"
    exit /b 1
)
exit /b 0

:find_python
set "PY_EXTRA="
if defined PYTHON (
    set "PY=%PYTHON%"
    if exist "%PYTHON%" goto :python_ok
    %PYTHON% -c "import sys" 2>nul
    if not errorlevel 1 goto :python_ok
)
if exist "%KIT%\.venv\Scripts\python.exe" (
    set "PY=%KIT%\.venv\Scripts\python.exe"
    goto :python_ok
)
if exist "%KIT%\..\.sign-venv\Scripts\python.exe" (
    set "PY=%KIT%\..\.sign-venv\Scripts\python.exe"
    goto :python_ok
)
py -3 -c "import sys" 2>nul
if not errorlevel 1 (
    set "PY=py"
    set "PY_EXTRA=-3"
    goto :python_ok
)
python -c "import sys" 2>nul
if not errorlevel 1 (
    set "PY=python"
    goto :python_ok
)
python3 -c "import sys" 2>nul
if not errorlevel 1 (
    set "PY=python3"
    goto :python_ok
)
echo ERROR: Python not found. Install Python 3 and tick "Add python.exe to PATH".
exit /b 1

:python_ok
exit /b 0

:make_venv
echo imgtool: creating %KIT%\.venv
rem Create it with the already-detected interpreter (may come from the PYTHON variable even if it is not on PATH)
set "VENV_PY=%PY%"
if /I "%VENV_PY%"=="%KIT%\.venv\Scripts\python.exe" set "VENV_PY="
if defined VENV_PY (
    "%VENV_PY%" -m venv "%KIT%\.venv" 2>nul
)
if exist "%KIT%\.venv\Scripts\python.exe" goto :venv_ready
py -3 -m venv "%KIT%\.venv" 2>nul
if exist "%KIT%\.venv\Scripts\python.exe" goto :venv_ready
python -m venv "%KIT%\.venv" 2>nul
if exist "%KIT%\.venv\Scripts\python.exe" goto :venv_ready
echo ERROR: cannot create %KIT%\.venv
echo   Install Python 3, or point to an interpreter (no need to add it to PATH):
echo     set PYTHON=^<absolute path to python.exe^>
exit /b 1
:venv_ready
set "PY=%KIT%\.venv\Scripts\python.exe"
set "PY_EXTRA="
"%PY%" -m pip install -q -r "%KIT%\requirements.txt"
if errorlevel 1 (
    echo ERROR: Python dependencies are missing. Run this first:
    echo   "%PY%" -m pip install -r "%KIT%\requirements.txt"
    exit /b 1
)
"%PY%" -c "import click, cryptography, cbor2, intelhex" 2>nul
if errorlevel 1 (
    echo ERROR: still cannot import click/cryptography/cbor2/intelhex after installing the dependencies
    exit /b 1
)
exit /b 0
