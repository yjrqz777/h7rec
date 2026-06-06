@echo off
setlocal

set "PROJECT_DIR=%~dp0"
set "SRC_DIR=%PROJECT_DIR%MDK-ARM\.vscode"
set "DST_DIR=%PROJECT_DIR%.vscode"

if not exist "%SRC_DIR%\" (
    echo Source folder not found: "%SRC_DIR%"
    exit /b 1
)

robocopy "%SRC_DIR%" "%DST_DIR%" /E /COPY:DAT /R:2 /W:1
if %ERRORLEVEL% GEQ 8 (
    echo Failed to copy "%SRC_DIR%" to "%DST_DIR%".
    exit /b %ERRORLEVEL%
)

echo Copied "%SRC_DIR%" to "%DST_DIR%".
exit /b 0
