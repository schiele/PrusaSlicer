@REM /|/ Copyright (c) preFlight 2025+ oozeBot, LLC
@REM /|/
@REM /|/ Released under AGPLv3 or higher
@REM /|/
@REM Wrapper to run build_deps.sh from Windows cmd prompt
@echo off
SET "GIT_BASH=%ProgramFiles%\Git\bin\bash.exe"
IF NOT EXIST "%GIT_BASH%" (
    echo ERROR: Git Bash not found at %GIT_BASH%
    exit /b 1
)
"%GIT_BASH%" "%~dp0build_deps.sh" %*
