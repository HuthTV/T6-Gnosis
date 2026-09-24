@echo off
setlocal
set "STAMINA_COMPILER=%~f1"
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "STAMINA_VS=%%i"
call "%STAMINA_VS%\Common7\Tools\VsDevCmd.bat" -arch=x86 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
if not exist "%~dp0build" mkdir "%~dp0build"
if not exist "%~dp0dist" mkdir "%~dp0dist"
pushd "%~dp0build"
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /WX /DNOMINMAX /LD /I../vendor/plutonium-sdk ../src/plugin.cpp /Foplugin.obj /link /OUT:../dist/T6StaminaViewer.dll /IMPLIB:T6StaminaViewer.lib /DYNAMICBASE /NXCOMPAT
if errorlevel 1 goto done
"%STAMINA_COMPILER%" -m comp -g t6 -s pc ..\scripts\stamina_hud.gsc
if errorlevel 1 goto done
copy /y compiled\t6\stamina_hud.gsc ..\dist\stamina_hud.gsc >nul
:done
set "STAMINA_RESULT=%errorlevel%"
popd
exit /b %STAMINA_RESULT%
