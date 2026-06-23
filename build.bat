@echo off
REM Build CustomStrayFramework.dll

set "DESTINATION_DIR=C:\Users\Pseudonym_Tim\Desktop\Tools\Mewtator\mods\CustomStrayFramework"
set "MEWTATOR_DEPLOY=true"

setlocal

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found. Is Visual Studio installed?
    pause
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do (
    set "VSDIR=%%i"
)

if not defined VSDIR (
    echo ERROR: Could not find a Visual Studio installation.
    pause
    exit /b 1
)

if not exist "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" (
    echo ERROR: vcvarsall.bat not found at "%VSDIR%\VC\Auxiliary\Build\"
    pause
    exit /b 1
)

call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1

echo Building CustomStrayFramework.dll...

cl /LD /O2 /MT /GS- /W3 /D_CRT_SECURE_NO_WARNINGS /TC src\main.c src\config.c src\string_utils.c src\cat_customization.c src\spawn.c src\spawn_coordinator.c /Fe:CustomStrayFramework.dll /link user32.lib

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Build FAILED.
    pause
    exit /b 1
)

del /Q *.obj CustomStrayFramework.lib CustomStrayFramework.exp 2>nul

if /I "%MEWTATOR_DEPLOY%"=="true" (
    set "DEPLOY_DIR=%DESTINATION_DIR%"
) else (
    set "DEPLOY_DIR=%DESTINATION_DIR%\mods"
)

if not exist "%DEPLOY_DIR%" (
    mkdir "%DEPLOY_DIR%"
)

copy /Y CustomStrayFramework.dll "%DEPLOY_DIR%\CustomStrayFramework.dll"
copy /Y CustomStrayFramework.ini "%DEPLOY_DIR%\CustomStrayFramework.ini"
copy /Y debug_spawns.ini "%DEPLOY_DIR%\debug_spawns.ini"
copy /Y custom_strays.example.ini "%DEPLOY_DIR%\custom_strays.example.ini"
copy /Y description.json "%DEPLOY_DIR%\description.json"
if exist preview.png copy /Y preview.png "%DEPLOY_DIR%\preview.png"

echo.
echo Build succeeded and deployed to %DEPLOY_DIR%

pause
