@echo off
:: ════════════════════════════════════════════════════════════════════════════
::  Photo of the Day item — called from update_all.bat  [PLACEHOLDER]
::
::  Usage:   call item_photo.bat <slot>
::  Example: call item_photo.bat 04
::
::  Requires env vars set by update_all.bat: TOOLS, TMP, IMAGES_DIR, AES_KEY
::  Output: %IMAGES_DIR%\<slot> - photo.enc
:: ════════════════════════════════════════════════════════════════════════════

set "SLOT=%~1"
if "%SLOT%"=="" ( echo ERROR: item_photo.bat requires slot as first arg & exit /b 1 )

echo [%SLOT%] Photo of the Day: not yet implemented.
echo          Add fetch + render logic here, then encrypt with:
echo          python "%%TOOLS%%\encrypt_image.py" --key %%AES_KEY%% --input "%%TMP%%\photo.bmp" --output "%%IMAGES_DIR%%\%%SLOT%% - photo.enc"
exit /b 1
