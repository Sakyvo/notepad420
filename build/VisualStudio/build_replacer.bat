@ECHO OFF
@rem Build notepad420-replacer.exe (Release x64 only; output under .target\bin).
SETLOCAL ENABLEEXTENSIONS
CD /D %~dp0

SET "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
FOR /F "delims=" %%A IN ('"%VSWHERE%" -latest -property installationPath -prerelease -version [16.0^,19.0^)') DO SET "VS_PATH=%%A"
IF NOT EXIST "%VS_PATH%" (
  ECHO [ERROR] Visual Studio not found.
  EXIT /B 1
)
SET "MSBUILD=%VS_PATH%\MSBuild\Current\Bin\MSBuild.exe"
IF NOT EXIST "%MSBUILD%" SET "MSBUILD=%VS_PATH%\MSBuild\Current\Bin\amd64\MSBuild.exe"

"%MSBUILD%" /nologo notepad420-replacer.vcxproj ^
  /property:Configuration=Release;Platform=x64 ^
  /consoleloggerparameters:Verbosity=minimal /nodeReuse:true
IF %ERRORLEVEL% NEQ 0 (
  ECHO [ERROR] replacer build failed
  EXIT /B 1
)
ECHO [OK] notepad420-replacer.exe built
EXIT /B 0