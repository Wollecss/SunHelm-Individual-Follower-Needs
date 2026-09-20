@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
set "SKYRIM_MODS_FOLDER=H:\Nolvus Awakening\MODS\mods"
set "PROJECT_DIR=H:\Nolvus Awakening\PROJECTS\SunHelmFollowerNeeds"
set "LOG=%PROJECT_DIR%\build_output.txt"
cd /d "%PROJECT_DIR%"

if not exist "build\release\CMakeCache.txt" (
    echo === CONFIGURE === > "%LOG%"
    cmake --preset release >> "%LOG%" 2>&1
    if errorlevel 1 (
        echo === CONFIGURE FAILED === >> "%LOG%"
        exit /b 1
    )
) else (
    echo === CONFIGURE SKIPPED, CMakeCache.txt exists === > "%LOG%"
)

echo === BUILD === >> "%LOG%"
cmake --build "build\release" --config Release >> "%LOG%" 2>&1
exit /b %errorlevel%
