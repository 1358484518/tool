@echo off
rem This file MUST use Windows CRLF line endings (LF-only breaks cmd.exe) and stay pure ASCII.
rem Production signing entry point: forces the signing server and refuses --local.
rem Manual use only for release images - it is deliberately NOT wired into any build step.
rem Profile: PROD_PROFILE from remote.conf, unless --profile is given explicitly.
setlocal EnableExtensions
call "%~dp0sign.bat" --prod %*
set "RC=%ERRORLEVEL%"
endlocal & exit /b %RC%
