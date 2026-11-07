@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0notify_release.ps1" -Action listen -Edition dev
pause
