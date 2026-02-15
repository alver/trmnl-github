@echo off
setlocal enabledelayedexpansion

set "BUILD_DIR=%~1"
if "%BUILD_DIR%"=="" set "BUILD_DIR=.build"

:: Ensure we're in a git repo with the required branches
git rev-parse --git-dir >nul 2>&1
if errorlevel 1 (
    echo Error: not a git repository >&2
    exit /b 1
)

git rev-parse --verify origin/upstream >nul 2>&1
if errorlevel 1 (
    echo Error: origin/upstream branch not found >&2
    exit /b 1
)

:: Clean previous build dir if exists
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
mkdir "%BUILD_DIR%"

:: Layer 1: upstream firmware
echo Extracting upstream firmware...
git archive origin/upstream | tar -xf - -C "%BUILD_DIR%"

:: Layer 2: overlay files from current branch (overwrites upstream where needed)
echo Applying overlay files...
git archive HEAD -- src/ include/ test/ platformio.ini | tar -xf - -C "%BUILD_DIR%"

echo.
echo Build directory ready: %BUILD_DIR%
echo.
echo   Build:   cd %BUILD_DIR% ^&^& pio run -e github_pages
echo   Test:    cd %BUILD_DIR% ^&^& pio test -e native-crypto
echo   Flash:   cd %BUILD_DIR% ^&^& pio run -e github_pages -t upload
echo   Clean:   rmdir /s /q %BUILD_DIR%
