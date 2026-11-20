@echo off
setlocal
cd /d "%~dp0"

rem Prefer the CMake that ships with Visual Studio over any other cmake on PATH (MSYS2, Cygwin, ...).
set "CMAKE=cmake"
set "VSW=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSW%" for /f "usebackq delims=" %%i in (`"%VSW%" -latest -products * -property installationPath`) do set "VSROOT=%%i"
if defined VSROOT if exist "%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

if "%CMAKE%"=="cmake" (
    where cmake >nul 2>nul
    if errorlevel 1 (
        echo CMake was not found. Install Visual Studio with Desktop development with C++, then run this again.
        goto :fail
    )
)
where git >nul 2>nul
if errorlevel 1 (
    echo git was not found. Install Git for Windows, then run this again.
    goto :fail
)

echo Using CMake: %CMAKE%
"%CMAKE%" -S . -B build -G "Visual Studio 18 2026" -A x64 > build_log.txt 2>&1
if not errorlevel 1 goto :configured
echo The Visual Studio 2026 generator did not work, trying Visual Studio 2022 ...
if exist build rmdir /s /q build
"%CMAKE%" -S . -B build -G "Visual Studio 17 2022" -A x64 >> build_log.txt 2>&1
if not errorlevel 1 goto :configured
echo Configure step failed:
type build_log.txt
goto :fail

:configured
"%CMAKE%" --build build --config Release >> build_log.txt 2>&1
if errorlevel 1 (
    echo Build step failed:
    type build_log.txt
    goto :fail
)

echo.
echo Built: %~dp0build\Release\GdMacroMenu.exe
pause
exit /b 0

:fail
echo.
echo FAILED. The full output is in build_log.txt next to this file. Send me that text.
pause
exit /b 1
