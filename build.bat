@echo off
rem SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
setlocal
if "%~1"=="" goto build
if /i "%~1"=="--help" goto help
if /i "%~1"=="--web" goto web
if /i not "%~1"=="--package" goto invalid
if not "%~2"=="" goto invalid
call :clean
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0sdk\Build.ps1" -Package
if errorlevel 1 exit /b %errorlevel%
call :alias
exit /b 0
:build
call :clean
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0sdk\Build.ps1"
if errorlevel 1 exit /b %errorlevel%
call :alias
exit /b 0
:invalid
echo Use build.bat --web, build.bat or build.bat --package.
exit /b 2
:web
if not "%~2"=="" goto invalid
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Build-Web.ps1"
exit /b %errorlevel%
:help
echo build.bat             Build firmware
echo build.bat --web       Build browser preview
echo build.bat --package   Build firmware and complete workbench
echo Toolchain and output paths: README.md
exit /b 0
rem DX747 names its outputs FM1_DX747.fwsc and FM1-DX747.html. The SDK scripts (hash-pinned, not edited) still write the upstream
rem names FM1_OFFICIAL_OPEN.fwsc and FM1-Official-Open-Source.html: the copies below are the same bytes under DX747's names.
:clean
del /q "%~dp0build\dx7\generated\FM1_DX747.fwsc" "%~dp0build\official-web\FM1-DX747.html" 2>nul
exit /b 0
:alias
if exist "%~dp0build\dx7\generated\FM1_OFFICIAL_OPEN.fwsc" copy /y "%~dp0build\dx7\generated\FM1_OFFICIAL_OPEN.fwsc" "%~dp0build\dx7\generated\FM1_DX747.fwsc" >nul
if exist "%~dp0build\official-web\FM1-Official-Open-Source.html" copy /y "%~dp0build\official-web\FM1-Official-Open-Source.html" "%~dp0build\official-web\FM1-DX747.html" >nul
exit /b 0
