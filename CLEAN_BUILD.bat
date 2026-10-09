@echo off
REM ===========================================================
REM Clean Build - opencirt-<Version>.brx (Windows)
REM   BRX SDK: external\brx_sdk oder Umgebungsvariable BRX_SDK_DIR
REM ===========================================================

cd /d "%~dp0"

echo.
echo ============================================================
echo Clean Build - opencirt-^<Version^>.brx (Windows)
echo ============================================================
echo.

REM Step 0: BricsCAD beenden - haelt sonst das geladene Plugin gesperrt
echo ============================================================
echo [STEP 0] Laufende BricsCAD-Instanzen beenden
echo ============================================================
set "CLOSE_ARGS="
if /I "%~1"=="/force" set "CLOSE_ARGS=-Force"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\Close-BricsCAD.ps1" %CLOSE_ARGS%
if errorlevel 1 (
    echo.
    echo BUILD ABGEBROCHEN - BricsCAD blockiert das geladene Plugin.
    pause
    exit /b 1
)
echo.

REM Step 1: Delete build directories
echo [STEP 1] Cleaning build directories...
if exist "build_windows" (
    rmdir /S /Q build_windows
    echo   Removed: build_windows
)
if exist "build" (
    rmdir /S /Q build
    echo   Removed: build
)
mkdir build_windows
echo   Created: build_windows
echo.

REM Step 2: CMake Configuration
echo ============================================================
echo [STEP 2] Running CMake Configuration
echo ============================================================
cd build_windows
cmake -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release ..
if errorlevel 1 (
    echo.
    echo ❌ CMAKE CONFIGURATION FAILED
    cd ..
    pause
    exit /b 1
)
echo ✅ CMake configuration SUCCESS
cd ..

echo.
echo ============================================================
echo [STEP 3] Building with MSBuild
echo ============================================================
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" build_windows\opencirt_plugin.vcxproj /p:Configuration=Release /m /v:minimal

if errorlevel 1 (
    echo.
    echo ============================================================
    echo ❌ BUILD FAILED - Check errors above
    echo ============================================================
    pause
    exit /b 1
)

echo.
echo ============================================================
echo ✅ BUILD SUCCESSFUL!
echo ============================================================
echo.
echo Output: build_windows\Release\opencirt-^<Version^>.brx
echo.
dir /b build_windows\Release\opencirt-*.brx
echo.
echo Kopie im Beispielprojekt: sample_project\00- BricsCAD Plugin\00- Windows Version\
dir /b "sample_project\00- BricsCAD Plugin\00- Windows Version\opencirt-*.brx"
echo.
echo Steht die Datei in der Startup Suite, dort den Eintrag auf die neue Version umstellen.
echo.
pause
