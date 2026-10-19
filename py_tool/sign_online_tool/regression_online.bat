@echo off
setlocal EnableDelayedExpansion

:: Online certificate regression for a chip that is already DA-locked.
:: Does not run debugauth=3. That call hangs inside SDMOpen on a closed chip.
:: The leaf private key stays on the sign server. ASCII only.

set "HERE=%~dp0"
set "PAUSE=1"

if /I "%~1"=="AUTO" set "PAUSE=0"

echo [1/2] sign server, certificate, probe
call :runpy "%HERE%regression_online.py" --check
if !errorlevel! neq 0 goto :error

echo [2/2] online full regression
call :runpy "%HERE%regression_online.py" --regress --per a
if !errorlevel! neq 0 goto :error

echo regression script success
if "%PAUSE%"=="1" cmd /k
exit /b 0

:runpy
python %*
if not errorlevel 9009 exit /b %errorlevel%
py -3 %*
exit /b %errorlevel%

:error
echo regression script failed.
if "%PAUSE%"=="1" cmd /k
exit /b 1
