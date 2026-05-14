@echo off
REM Build MewFurnitureFramework.dll with MSVC x64

setlocal
set "DESTINATION_DIR="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found. Install Visual Studio with the Desktop development with C++ workload.
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

call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1

if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Could not initialize the x64 MSVC environment.
    pause
    exit /b 1
)

echo Building MewFurnitureFramework.dll...
cl /LD /O2 /GS- /W3 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /TC src\MewFurnitureFramework.c /Fe:MewFurnitureFramework.dll /link kernel32.lib user32.lib

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Build FAILED.
    pause
    exit /b 1
)

del /Q MewFurnitureFramework.obj MewFurnitureFramework.lib MewFurnitureFramework.exp 2>nul

echo.
echo Build succeeded: MewFurnitureFramework.dll

if not "%DESTINATION_DIR%"=="" (
    if not exist "%DESTINATION_DIR%" mkdir "%DESTINATION_DIR%"
    copy /Y MewFurnitureFramework.dll "%DESTINATION_DIR%\MewFurnitureFramework.dll"
    copy /Y description.json "%DESTINATION_DIR%\description.json"
    echo Deployed to %DESTINATION_DIR%
)
