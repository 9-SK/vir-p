@echo off
setlocal
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`%VSWHERE% -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2^>nul`) do set VSPATH=%%i
if not defined VSPATH set VSPATH=C:\Program Files\Microsoft Visual Studio\2022\Community
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
MSBuild.exe "%~dp0Viridian.vcxproj" /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal
if exist "%~dp0Build\Viridian.dll" copy /y "%~dp0Build\Viridian.dll" "%~dp0..\bin\ViridianBase3.dll" >nul
endlocal
