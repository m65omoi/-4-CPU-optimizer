@echo off
setlocal

echo ======================================================================
echo   Building HOI4 REA-Guided Performance Optimizer (version.dll)
echo ======================================================================

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERROR] Visual Studio Installer / vswhere.exe not found!
    pause
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VSINSTALLDIR=%%i"
)

if not exist "%VSINSTALLDIR%\VC\Auxiliary\Build\vcvarsall.bat" (
    echo [ERROR] vcvarsall.bat not found in: %VSINSTALLDIR%
    pause
    exit /b 1
)

echo [+] Initializing MSVC x64 build environment...
call "%VSINSTALLDIR%\VC\Auxiliary\Build\vcvarsall.bat" x64

cd /d "%~dp0"
if not exist "bin" mkdir "bin"

echo [+] Compiling src\hoi4_optimizer.cpp...
cl /nologo /LD /O2 /MT /EHsc /W3 /Fe:bin\version.dll src\hoi4_optimizer.cpp

if %ERRORLEVEL% equ 0 (
    echo.
    echo ======================================================================
    echo [SUCCESS] Successfully built bin\version.dll!
    echo ======================================================================
    del /f /q bin\version.lib bin\version.exp 2>nul
) else (
    echo.
    echo [FAILED] Compilation failed with error code %ERRORLEVEL%.
)

endlocal
