@REM /|/ Copyright (c) preFlight 2025+ oozeBot, LLC
@REM /|/
@REM /|/ Released under AGPLv3 or higher
@REM /|/
@REM Windows entry point for build.sh
@REM Sets up MSVC compiler environment, then delegates to build.sh
@echo off
setlocal EnableDelayedExpansion

REM Find Git Bash
SET "GIT_BASH=%ProgramFiles%\Git\bin\bash.exe"
IF NOT EXIST "%GIT_BASH%" (
    echo ERROR: Git Bash not found at %GIT_BASH%
    exit /b 1
)

REM Find Visual Studio via vswhere
SET "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
IF NOT EXIST "%VSWHERE%" SET "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
IF NOT EXIST "%VSWHERE%" (
    echo ERROR: Visual Studio not found - vswhere.exe missing
    exit /b 1
)
FOR /F "tokens=* USEBACKQ" %%I IN (`"%VSWHERE%" -latest -property installationPath`) DO SET MSVC_DIR=%%I

REM Detect architecture
SET ARCH=x64
echo !PROCESSOR_IDENTIFIER! | findstr /I "ARM" >nul
IF !ERRORLEVEL! == 0 SET ARCH=arm64

REM Set up MSVC environment (cl.exe, link.exe, Windows SDK)
call "%MSVC_DIR%\Common7\Tools\vsdevcmd.bat" -arch=%ARCH% -host_arch=%ARCH% -app_platform=Desktop
IF %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to set up MSVC environment.
    exit /b 1
)
@echo off

REM Hand off to build.sh with all original arguments
"%GIT_BASH%" "%~dp0build.sh" %*
