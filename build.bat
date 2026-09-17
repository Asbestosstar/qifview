@echo off
setlocal EnableExtensions

rem QIF Viewer build wrapper for Windows.
rem Environment variables:
rem   BUILD_DIR       Build directory (default: build)
rem   BUILD_TYPE      CMake build type (default: Release)
rem   CMAKE_GENERATOR Optional CMake generator, e.g. Ninja or Visual Studio 17 2022
rem   JOBS            Parallel build jobs (default: NUMBER_OF_PROCESSORS)
rem
rem Any command-line arguments are forwarded to the CMake configure step.

set "ROOT_DIR=%~dp0"
if "%ROOT_DIR:~-1%"=="\" set "ROOT_DIR=%ROOT_DIR:~0,-1%"

if not defined BUILD_DIR set "BUILD_DIR=%ROOT_DIR%\build"
if not defined BUILD_TYPE set "BUILD_TYPE=Release"
if not defined JOBS set "JOBS=%NUMBER_OF_PROCESSORS%"
if not defined JOBS set "JOBS=1"

where cmake >nul 2>nul
if errorlevel 1 (
    echo error: cmake was not found in PATH 1>&2
    exit /b 1
)

echo Configuring QIF Viewer:
echo   source:     %ROOT_DIR%
echo   build:      %BUILD_DIR%
echo   build type: %BUILD_TYPE%
echo   jobs:       %JOBS%

if defined CMAKE_GENERATOR (
    cmake -S "%ROOT_DIR%" -B "%BUILD_DIR%" -G "%CMAKE_GENERATOR%" -DCMAKE_BUILD_TYPE="%BUILD_TYPE%" %*
) else (
    cmake -S "%ROOT_DIR%" -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE="%BUILD_TYPE%" %*
)
if errorlevel 1 exit /b %errorlevel%

cmake --build "%BUILD_DIR%" --config "%BUILD_TYPE%" --parallel %JOBS%
if errorlevel 1 exit /b %errorlevel%

echo.
echo Build complete.
echo Viewer:  %BUILD_DIR%\qifviewer.exe
echo Checker: %BUILD_DIR%\qifcheck.exe
echo.
echo Examples:
echo   "%BUILD_DIR%\qifviewer.exe" part.qif
echo   "%BUILD_DIR%\qifviewer.exe" --renderer vulkan part.qif
echo   "%BUILD_DIR%\qifcheck.exe" part.qif

endlocal
