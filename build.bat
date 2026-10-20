@echo off
rem Builds T6Gnosis.dll (x86 Release) from a clean build folder and runs the unit tests.
rem   build.bat                     development build, version "dev"   -> build\T6Gnosis.dll
rem   build.bat release [version]   release build, version = today's date (YYYY.MM.DD)
rem                                 unless given  -> dist\T6Gnosis.dll and dist\T6Gnosis-<version>.zip
rem Needs Visual Studio (Desktop development with C++) and gsc-tool (see generate.ps1).
setlocal
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
cl /nologo /c /O2 /MT /W3 /DNDEBUG /DWIN32 /D_WINDOWS /I../vendor/minhook-1.3.4/include ../vendor/minhook-1.3.4/src/buffer.c ../vendor/minhook-1.3.4/src/hook.c ../vendor/minhook-1.3.4/src/trampoline.c ../vendor/minhook-1.3.4/src/hde/hde32.c
if errorlevel 1 goto failed
cl /nologo /c /std:c++17 /EHsc /O2 /MT /W3 /DNDEBUG /DWIN32 /D_WINDOWS /I../vendor/imgui-1.91.9b ../vendor/imgui-1.91.9b/imgui.cpp ../vendor/imgui-1.91.9b/imgui_draw.cpp ../vendor/imgui-1.91.9b/imgui_tables.cpp ../vendor/imgui-1.91.9b/imgui_widgets.cpp ../vendor/imgui-1.91.9b/backends/imgui_impl_dx11.cpp ../vendor/imgui-1.91.9b/backends/imgui_impl_win32.cpp
if errorlevel 1 goto failed
cl /nologo /c /std:c++17 /EHsc /O2 /MT /W4 /WX /DNDEBUG /DWIN32 /D_WINDOWS /DNOMINMAX /I. /I../vendor/plutonium-sdk /I../vendor/imgui-1.91.9b /I../vendor/minhook-1.3.4/include ../src/plugin.cpp ../src/reader.cpp ../src/overlay.cpp ../src/zombies.cpp ../src/spawns.cpp ../src/spawns_native.cpp ../src/practice.cpp ../src/despawn.cpp ../src/despawn_schedule.cpp ../src/despawn_overlay.cpp ../src/staff_aid.cpp ../src/mob_aid.cpp ../src/world_collision.cpp ../src/collision_gpu.cpp ../src/frame_trace.cpp
if errorlevel 1 goto failed
rc /nologo /i . /fo T6Gnosis.res ../src/T6Gnosis.rc
if errorlevel 1 goto failed
rem Relocatable image with ASLR; /Brepro makes identical sources give an identical DLL.
link /nologo /DLL /MACHINE:X86 /DYNAMICBASE /BASE:0x62000000 /INCREMENTAL:NO /Brepro /OUT:T6Gnosis.dll plugin.obj reader.obj overlay.obj zombies.obj spawns.obj spawns_native.obj practice.obj despawn.obj despawn_schedule.obj despawn_overlay.obj staff_aid.obj mob_aid.obj world_collision.obj collision_gpu.obj frame_trace.obj imgui.obj imgui_draw.obj imgui_tables.obj imgui_widgets.obj imgui_impl_dx11.obj imgui_impl_win32.obj buffer.obj hook.obj trampoline.obj hde32.obj T6Gnosis.res user32.lib gdi32.lib
if errorlevel 1 goto failed
for %%t in (projection_tests zombie_tests spawn_tests spawns_native_tests despawn_tests jump_timing_tests playable_area_tests) do (
    cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /WX /DNOMINMAX /I../src ../tests/%%t.cpp /Fe:%%t.exe >nul
    if errorlevel 1 goto failed
    %%t.exe
    if errorlevel 1 goto failed
)
cd ..
if defined T6GNOSIS_VERSION (
    if not exist dist mkdir dist
    copy /y build\T6Gnosis.dll dist\T6Gnosis.dll >nul
    if errorlevel 1 goto failed
    rem The download carries the version in its name; the DLL inside stays T6Gnosis.dll so
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
