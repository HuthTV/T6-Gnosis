@echo off
rem Builds T6Gnosis.dll (x86 Release) from a clean build folder and runs the unit tests.
rem   build.bat                     development build, version "dev"   -> build\T6Gnosis.dll
rem   build.bat release [version]   release build, version = today's date (YYYY.MM.DD)
rem                                 unless given  -> dist\T6Gnosis.dll and dist\T6Gnosis-<version>.zip
rem Needs Visual Studio (Desktop development with C++) and gsc-tool (see generate.ps1).
setlocal

rem Plugin sources (src\), third-party sources and the unit tests (tests\<name>.cpp).
set "SOURCES=plugin reader overlay overlay_settings overlay_menu overlay_hud overlay_world overlay_aids movement zombies spawns spawns_native despawn despawn_schedule despawn_overlay staff_aid mob_aid grenade_aid robot_aid avogadro_aid world_collision collision_gpu frame_trace"
set "MINHOOK=buffer hook trampoline hde/hde32"
set "IMGUI=imgui imgui_draw imgui_tables imgui_widgets backends/imgui_impl_dx11 backends/imgui_impl_win32"
set "TESTS=projection_tests zombie_tests spawn_tests spawns_native_tests despawn_tests jump_timing_tests playable_area_tests avogadro_tests grenade_tests robot_tests"

set "T6GNOSIS_VERSION="
if /i "%~1"=="release" (
    if "%~2"=="" (
        for /f %%v in ('powershell.exe -NoProfile -Command "Get-Date -Format yyyy.MM.dd"') do set "T6GNOSIS_VERSION=%%v"
    ) else set "T6GNOSIS_VERSION=%~2"
)
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "T6GNOSIS_VS=%%i"
if not defined T6GNOSIS_VS (
    echo Visual Studio Build Tools with Desktop development with C++ is required.
    exit /b 1
)
call "%T6GNOSIS_VS%\Common7\Tools\VsDevCmd.bat" -arch=x86 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
pushd "%~dp0"
if exist build rmdir /s /q build
mkdir build

rem The compiled GSC helper is embedded in the DLL (src\helper_deploy.hpp includes build\helper_gsc.inc).
if defined T6GNOSIS_VERSION (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File generate.ps1 -Version %T6GNOSIS_VERSION%
) else (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File generate.ps1
)
if errorlevel 1 goto failed

cd build
set "COMPILE=/nologo /c /O2 /MT /DNDEBUG /DWIN32 /D_WINDOWS"
set "PLUGIN_FILES="
set "MINHOOK_FILES="
set "IMGUI_FILES="
set "OBJECTS="
for %%f in (%SOURCES%) do (
    call set "PLUGIN_FILES=%%PLUGIN_FILES%% ../src/%%f.cpp"
    call set "OBJECTS=%%OBJECTS%% %%f.obj"
)
for %%f in (%MINHOOK%) do (
    call set "MINHOOK_FILES=%%MINHOOK_FILES%% ../vendor/minhook-1.3.4/src/%%f.c"
    call set "OBJECTS=%%OBJECTS%% %%~nf.obj"
)
for %%f in (%IMGUI%) do (
    call set "IMGUI_FILES=%%IMGUI_FILES%% ../vendor/imgui-1.91.9b/%%f.cpp"
    call set "OBJECTS=%%OBJECTS%% %%~nf.obj"
)
cl %COMPILE% /W3 /I../vendor/minhook-1.3.4/include %MINHOOK_FILES%
if errorlevel 1 goto failed
cl %COMPILE% /std:c++17 /EHsc /W3 /I../vendor/imgui-1.91.9b %IMGUI_FILES%
if errorlevel 1 goto failed
cl %COMPILE% /std:c++17 /EHsc /W4 /WX /DNOMINMAX /I. /I../vendor/plutonium-sdk /I../vendor/imgui-1.91.9b /I../vendor/minhook-1.3.4/include %PLUGIN_FILES%
if errorlevel 1 goto failed
rc /nologo /i . /fo T6Gnosis.res ../src/T6Gnosis.rc
if errorlevel 1 goto failed
rem Relocatable image with ASLR. /Brepro makes identical sources give an identical DLL.
link /nologo /DLL /MACHINE:X86 /DYNAMICBASE /BASE:0x62000000 /INCREMENTAL:NO /Brepro /OUT:T6Gnosis.dll %OBJECTS% T6Gnosis.res user32.lib gdi32.lib
if errorlevel 1 goto failed

for %%t in (%TESTS%) do (
    cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /WX /DNOMINMAX /I../src ../tests/%%t.cpp /Fe:%%t.exe >nul
    if errorlevel 1 goto failed
    .\%%t.exe
    if errorlevel 1 goto failed
)
cd ..

if defined T6GNOSIS_VERSION (
    if not exist dist mkdir dist
    copy /y build\T6Gnosis.dll dist\T6Gnosis.dll >nul
    if errorlevel 1 goto failed
    rem The download carries the version in its name. The DLL inside stays T6Gnosis.dll so
    rem an update replaces the old file instead of loading a second copy.
    powershell.exe -NoProfile -Command "Compress-Archive -Force -Path dist\T6Gnosis.dll,README.md -DestinationPath dist\T6Gnosis-%T6GNOSIS_VERSION%.zip"
    if errorlevel 1 goto failed
    echo Built T6 Gnosis %T6GNOSIS_VERSION%: dist\T6Gnosis.dll and dist\T6Gnosis-%T6GNOSIS_VERSION%.zip
) else echo Built T6 Gnosis dev: build\T6Gnosis.dll
popd
exit /b 0

:failed
popd
exit /b 1
