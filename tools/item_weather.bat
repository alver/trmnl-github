@echo off
:: ════════════════════════════════════════════════════════════════════════════
::  Weather item — called from update_all.bat
::
::  Usage:   call item_weather.bat <slot> <city>
::  Example: call item_weather.bat 01 munich
::           call item_weather.bat 03 taganrog
::
::  Requires env vars set by update_all.bat: TOOLS, TMP, IMAGES_DIR, AES_KEY
::  Output: %IMAGES_DIR%\<slot> - weather_<city>.enc
:: ════════════════════════════════════════════════════════════════════════════

set "SLOT=%~1"
set "CITY=%~2"

if "%SLOT%"=="" ( echo ERROR: item_weather.bat requires slot as first arg  & exit /b 1 )
if "%CITY%"=="" ( echo ERROR: item_weather.bat requires city as second arg & exit /b 1 )

set "STEM=weather_%CITY%"
set "BMP_FILE=%TMP%\%STEM%.bmp"
set "ENC_FILE=%IMAGES_DIR%\%SLOT% - %STEM%.enc"

echo [%SLOT%] Rendering weather: %CITY%...
python "%TOOLS%\weather.py" --city "%CITY%" --out-dir "%TMP%"
if errorlevel 1 ( echo ERROR: weather.py failed for city '%CITY%' & exit /b 1 )

if not exist "%BMP_FILE%" (
    echo ERROR: expected BMP not found: %BMP_FILE%
    exit /b 1
)

echo [%SLOT%] Encrypting...
python "%TOOLS%\encrypt_image.py" --key %AES_KEY% --input "%BMP_FILE%" --output "%ENC_FILE%"
if errorlevel 1 ( echo ERROR: encrypt_image.py failed & exit /b 1 )

echo [%SLOT%] Done ^-^> %ENC_FILE%

:: Clean tmp files for this item
del /q "%TMP%\%STEM%.html" 2>nul
del /q "%TMP%\%STEM%.png"  2>nul
del /q "%BMP_FILE%"        2>nul
