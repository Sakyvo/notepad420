@ECHO OFF
@rem Build notepad420 installer with Inno Setup (ISCC.exe).
@rem Output: .target\.distribution\installer\notepad420-setup-x64-<version>.exe
SETLOCAL ENABLEEXTENSIONS
CD /D %~dp0

SET "ISCC=%ProgramFiles%\Inno Setup 7\ISCC.exe"
IF NOT EXIST "%ISCC%" SET "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
IF NOT EXIST "%ISCC%" SET "ISCC=%ProgramFiles(x86)%\Inno Setup 5\ISCC.exe"
IF NOT EXIST "%ISCC%" (
  ECHO [ERROR] Inno Setup not found (ISCC.exe^).
  EXIT /B 1
)

IF NOT EXIST "..\..\.target\.distribution\app\notepad420.exe" (
  ECHO [ERROR] .target\.distribution\app\notepad420.exe missing. Build it first.
  EXIT /B 1
)

IF NOT EXIST "..\..\.target\.distribution\installer" MD "..\..\.target\.distribution\installer"

"%ISCC%" make_installer.iss
IF %ERRORLEVEL% NEQ 0 (
  ECHO [ERROR] installer build failed
  EXIT /B 1
)
ECHO [OK] installer built
EXIT /B 0
