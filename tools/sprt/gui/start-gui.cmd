@echo off
REM SPRT Test Arayuzu baslatici. Cift tiklayin.
REM serve.ps1 yerel web sunucusunu baslatir ve tarayiciyi otomatik acar.
REM Sunucuyu durdurmak: bu pencereyi kapatin (ya da Ctrl+C).
title SPRT Test Arayuzu
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0serve.ps1"
echo.
echo Sunucu durdu. Kapatmak icin bir tusa basin.
pause >nul
