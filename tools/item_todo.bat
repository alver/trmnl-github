@echo off
:: ════════════════════════════════════════════════════════════════════════════
::  TODO item — called from update_all.bat  [PLACEHOLDER — not yet implemented]
::
::  Usage:   call item_todo.bat <slot>
::  Example: call item_todo.bat 03
::
::  Requires env vars set by update_all.bat: TOOLS, TMP, IMAGES_DIR, AES_KEY
::  Output: %IMAGES_DIR%\<slot> - todo.enc
:: ════════════════════════════════════════════════════════════════════════════

set "SLOT=%~1"
if "%SLOT%"=="" ( echo ERROR: item_todo.bat requires slot as first arg & exit /b 1 )

echo [%SLOT%] TODO: not yet implemented.
echo          Add rendering logic here, then encrypt with:
echo          python "%%TOOLS%%\encrypt_image.py" --key %%AES_KEY%% --input "%%TMP%%\todo.bmp" --output "%%IMAGES_DIR%%\%%SLOT%% - todo.enc"
exit /b 1
