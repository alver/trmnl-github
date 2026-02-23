@echo off
setlocal enabledelayedexpansion

:: ── Must be launched from the repo root: .\tools\update_weather.bat ──
:: ── ImageMagick path ───────────────────────────────────────────
set PATH=c:\utils\ImageMagick-7.0.10-Q16-HDRI;%PATH%

:: ── Configuration ──────────────────────────────────────────────
:: Pass city as first argument, e.g.: update_weather.bat berlin
:: Defaults to "munich" if omitted.
set "ROOT=%CD%"
set "CONTENT_DIR=%ROOT%\my_content"
set "CONTENT_REPO=git@github.com:alver/trmnl-content.git"
set "KEY_FILE=%ROOT%\.key"
set "TOOLS=%ROOT%\tools"
set "TMP=%ROOT%\tmp"

if "%~1"=="" ( set "CITY=munich" ) else ( set "CITY=%~1" )
set "BMP_FILE=%TMP%\weather_%CITY%.bmp"
set "ENC_FILE=%CONTENT_DIR%\images\weather_%CITY%.enc"

:: ── Activate venv ────────────────────────────────────────────
if not exist "%ROOT%\venv\Scripts\activate.bat" (
    echo ERROR: venv not found at %ROOT%\venv
    exit /b 1
)
call "%ROOT%\venv\Scripts\activate.bat"

:: ── Create tmp dir ───────────────────────────────────────────
if not exist "%TMP%" mkdir "%TMP%"

:: ── Step 1-2: Clone or update content repo ────────────────────
if not exist "%CONTENT_DIR%\.git" (
    echo Cloning content repo...
    git clone "%CONTENT_REPO%" "%CONTENT_DIR%"
    if errorlevel 1 ( echo ERROR: git clone failed & exit /b 1 )
) else (
    echo Updating content repo...
    pushd "%CONTENT_DIR%"
    git fetch origin
    git reset --hard origin/main
    git clean -fd
    popd
)

:: ── Step 3: Render weather image ──────────────────────────────
echo Rendering weather for %CITY%...
python "%TOOLS%\weather.py" --city "%CITY%" --out-dir "%TMP%"
if errorlevel 1 ( echo ERROR: weather.py failed & exit /b 1 )

if not exist "%BMP_FILE%" (
    echo ERROR: %BMP_FILE% was not created
    exit /b 1
)

:: ── Step 4: Encrypt the BMP ───────────────────────────────────
if not exist "%KEY_FILE%" (
    echo ERROR: .key file not found at %KEY_FILE%
    exit /b 1
)
set /p AES_KEY=<"%KEY_FILE%"

echo Encrypting...
python "%TOOLS%\encrypt_image.py" --key %AES_KEY% --input "%BMP_FILE%" --output "%ENC_FILE%"
if errorlevel 1 ( echo ERROR: encrypt_image.py failed & exit /b 1 )

:: ── Step 5: Orphan commit + force push ────────────────────────
echo Pushing to content repo...
pushd "%CONTENT_DIR%"
git checkout --orphan temp_branch
git add -A
git commit -m "weather update %date% %time%"
git branch -M temp_branch main
git push --force origin main
popd

:: ── Step 6: Cleanup tmp ─────────────────────────────────────
echo Cleaning up...
del /q "%TMP%\weather_%CITY%.html" 2>nul
del /q "%TMP%\weather_%CITY%.png"  2>nul
del /q "%BMP_FILE%"                2>nul

echo Done.
