@echo off
setlocal

REM ============================================================
REM  Auto-detect the newest installed Visual Studio (x64)
REM  and generate the CMake project.
REM  Supports VS 2019, 2022, 2026.
REM  The project requires "Desktop development with C++" 
REM  and "Game development with C++" workloads installed.
REM ============================================================

REM Locate vswhere.exe (the official VS detection tool)
REM https://learn.microsoft.com/en-us/visualstudio/install/tools-for-managing-visual-studio-instances?view=visualstudio
set "VSWHERE="
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not defined VSWHERE if exist "%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not defined VSWHERE if exist "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" set "VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"

if not defined VSWHERE (
    echo [ERROR] vswhere.exe not found. Please install Visual Studio and then run this batch file again.
    pause
    exit /b 1
)

REM Ask vswhere for the newest install with the C++ workload.
REM Each -requires is a workload ID; -products * matches any SKU.
set "VSWHERE_REQUIRES=Microsoft.VisualStudio.Workload.NativeDesktop Microsoft.VisualStudio.Workload.NativeGame"

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires %VSWHERE_REQUIRES% -property installationPath`) do (
    set "VS_INSTALL_PATH=%%i"
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires %VSWHERE_REQUIRES% -property installationVersion`) do (
    set "VS_VERSION=%%i"
)

if not defined VS_INSTALL_PATH (
    echo [ERROR] No Visual Studio installation with the C++ and Gamedev workload was found.
    echo Install Visual Studio with the following workloads:
    echo Desktop development with C++
    echo Game development with C++
    pause
    exit /b 1
)

if not defined VS_VERSION set "VS_VERSION=17.0"

REM Major version -> year + generator name.
for /f "tokens=1 delims=." %%v in ("%VS_VERSION%") do set "VS_MAJOR=%%v"

set "VS_YEAR="
set "GENERATOR="
if "%VS_MAJOR%"=="16" ( set "VS_YEAR=2019" & set "GENERATOR=Visual Studio 16 2019" )
if "%VS_MAJOR%"=="17" ( set "VS_YEAR=2022" & set "GENERATOR=Visual Studio 17 2022" )
if "%VS_MAJOR%"=="18" ( set "VS_YEAR=2026" & set "GENERATOR=Visual Studio 18 2026" )

if not defined GENERATOR (
    echo [WARN] Unrecognized VS major version "%VS_MAJOR%". Falling back to VS 2022.
    set "VS_YEAR=2022"
    set "GENERATOR=Visual Studio 17 2022"
)

echo Detected:   Visual Studio %VS_YEAR%  (version %VS_VERSION%)
echo Path:       %VS_INSTALL_PATH%
echo Generator:  %GENERATOR%
echo.

REM Run CMake.
if not exist CMakeLists.txt (
    echo [ERROR] No CMakeLists.txt in current directory.
    pause
    exit /b 1
)

if not exist "build" mkdir "build"
pushd "build"
if errorlevel 1 ( popd & echo [ERROR] Cannot enter build directory & pause & exit /b 1 )

cmake -G "%GENERATOR%" -A x64 ..
if errorlevel 1 ( popd & echo [ERROR] CMake configuration failed & pause & exit /b 1 )

echo.
popd
endlocal
pause
exit /b 0
