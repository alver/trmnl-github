@echo off
setlocal enabledelayedexpansion

:: ════════════════════════════════════════════════════════════════════════════
::  TRMNL content orchestration — run from repo root: .\tools\update_all.bat
:: ════════════════════════════════════════════════════════════════════════════

set "ROOT=%CD%"
set "CONTENT_DIR=%ROOT%\my_content"
set "CONTENT_REPO=git@github.com:alver/trmnl-content.git"
set "KEY_FILE=%ROOT%\.key"
set "TOOLS=%ROOT%\tools"
set "TMP=%ROOT%\tmp"
set "IMAGES_DIR=%CONTENT_DIR%\images"

:: ── ImageMagick ──────────────────────────────────────────────────────────────
set PATH=c:\utils\ImageMagick-7.0.10-Q16-HDRI;%PATH%

:: ── Activate venv ────────────────────────────────────────────────────────────
if not exist "%ROOT%\venv\Scripts\activate.bat" (
    echo ERROR: venv not found at %ROOT%\venv
    exit /b 1
)
call "%ROOT%\venv\Scripts\activate.bat"

:: ── Create tmp dir ───────────────────────────────────────────────────────────
if not exist "%TMP%" mkdir "%TMP%"

:: ── Clone or update content repo ─────────────────────────────────────────────
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

:: ── Ensure images dir exists and is cleared (only generated files go in) ─────
if not exist "%IMAGES_DIR%" mkdir "%IMAGES_DIR%"
::for %%f in ("%IMAGES_DIR%\*.enc") do del /q "%%f"

:: ── Load AES key ─────────────────────────────────────────────────────────────
if not exist "%KEY_FILE%" (
    echo ERROR: .key file not found at %KEY_FILE%
    exit /b 1
)
set /p AES_KEY=<"%KEY_FILE%"

echo.
echo ============================================================================
echo  ITEMS  (edit below to add / remove / reorder screens)
echo  Slot number controls display order on device: 01 shown first, 02 second…
echo ============================================================================
echo.

:: ── Item list ─────────────────────────────────────────────────────────────────
:: Format:
::   call "%TOOLS%\item_weather.bat"  <slot>  <city>
::   call "%TOOLS%\item_todo.bat"     <slot>
::   call "%TOOLS%\item_photo.bat"    <slot>
::
:: To disable an item: comment it out (and its errorlevel check) with ::

call "%TOOLS%\item_weather.bat" 01 munich
if errorlevel 1 ( echo ERROR in item 01 & exit /b 1 )

call "%TOOLS%\item_weather.bat" 02 taganrog
if errorlevel 1 ( echo ERROR in item 02 & exit /b 1 )

::call "%TOOLS%\item_todo.bat" 03
::if errorlevel 1 ( echo ERROR in item 03 & exit /b 1 )

::call "%TOOLS%\item_photo.bat" 04
::if errorlevel 1 ( echo ERROR in item 04 & exit /b 1 )

:: ── Update manifest ───────────────────────────────────────────────────────────
echo.
echo ============================================================================
echo  Updating manifest...
echo ============================================================================
python "%TOOLS%\update_manifest.py" ^
    --key %AES_KEY% ^
    --images-dir "%IMAGES_DIR%" ^
    --output "%CONTENT_DIR%\manifest.enc"
if errorlevel 1 ( echo ERROR: update_manifest.py failed & exit /b 1 )

:: ── Commit and force-push content repo ───────────────────────────────────────
echo.
echo ============================================================================
echo  Pushing to content repo...
echo ============================================================================
pushd "%CONTENT_DIR%"
git checkout --orphan temp_branch
git add -A
git commit -m "content update %date% %time%"
git branch -M temp_branch main
git push --force origin main
popd
if errorlevel 1 ( echo ERROR: git push failed & exit /b 1 )

:: ── Clean up tmp ─────────────────────────────────────────────────────────────
echo.
echo Cleaning up tmp...
for %%f in ("%TMP%\*") do del /q "%%f" 2>nul

echo.
echo All done!
