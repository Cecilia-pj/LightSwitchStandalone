@echo off
rem ---------------------------------------------------------------------------
rem Build LightSwitchStandalone without Visual Studio / without a solution file.
rem
rem Usage:  build.bat [Debug|Release]
rem
rem Requirements: Visual Studio 2022 Build Tools with the "Desktop development
rem with C++" workload (MSVC v143 + Windows 11 SDK).
rem ---------------------------------------------------------------------------
setlocal

set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=Release

set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if not exist "%VSWHERE%" (
    echo [build] vswhere.exe not found. Install Visual Studio 2022 Build Tools.
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSPATH=%%i
if not defined VSPATH (
    echo [build] No Visual Studio installation with the C++ toolset was found.
    exit /b 1
)

set MSBUILD=%VSPATH%\MSBuild\Current\Bin\MSBuild.exe
if not exist "%MSBUILD%" (
    echo [build] MSBuild.exe not found at "%MSBUILD%".
    exit /b 1
)

echo [build] Using %MSBUILD%
echo [build] Configuration=%CONFIG% Platform=x64

"%MSBUILD%" "%~dp0LightSwitchStandalone.vcxproj" /nologo /m ^
    /p:Configuration=%CONFIG% /p:Platform=x64 ^
    /v:minimal

if errorlevel 1 (
    echo [build] FAILED
    exit /b 1
)

echo.
echo [build] OK -^> %~dp0bin\%CONFIG%\LightSwitchStandalone.exe
endlocal
