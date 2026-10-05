@ECHO OFF
@rem Build and run the notepad420 unit tests (console only, no GUI, no window).
@rem Output goes under .target\tests\ per the build-output contract.
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D %~dp0..

SET "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
FOR /F "delims=" %%A IN ('"%VSWHERE%" -latest -property installationPath -prerelease -version [16.0^,19.0^)') DO SET "VS_PATH=%%A"
IF NOT EXIST "%VS_PATH%" (
  ECHO [ERROR] Visual Studio not found.
  EXIT /B 1
)

CALL "%VS_PATH%\Common7\Tools\vsdevcmd" -no_logo -arch=amd64 >NUL
IF NOT EXIST ".target\tests\obj" MD ".target\tests\obj"

SET "RC=0"

CL /nologo /std:c++17 /EHsc /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /Fe:.target\tests\paste_policy_test.exe /Fo:.target\tests\obj\ tools\tests\paste_policy_test.cpp src\PasteCache.cpp src\PasteFile.cpp src\PasteImage.cpp src\Base64.cpp /I src /I scintilla\include gdi32.lib user32.lib kernel32.lib gdiplus.lib shlwapi.lib shell32.lib ole32.lib advapi32.lib
IF !ERRORLEVEL! NEQ 0 ( ECHO [ERROR] paste_policy_test compilation failed & SET "RC=1" ) ELSE (
  ".target\tests\paste_policy_test.exe"
  IF !ERRORLEVEL! NEQ 0 SET "RC=1"
)

CL /nologo /std:c++17 /EHsc /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /Fe:.target\tests\paste_cache_test.exe /Fo:.target\tests\obj\ tools\tests\paste_cache_test.cpp src\PasteCache.cpp src\Base64.cpp /I src /I scintilla\include gdi32.lib user32.lib kernel32.lib shlwapi.lib shell32.lib gdiplus.lib
IF !ERRORLEVEL! NEQ 0 ( ECHO [ERROR] paste_cache_test compilation failed & SET "RC=1" ) ELSE (
  ".target\tests\paste_cache_test.exe"
  IF !ERRORLEVEL! NEQ 0 SET "RC=1"
)

CL /nologo /std:c++17 /EHsc /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /Fe:.target\tests\paste_file_uri_test.exe /Fo:.target\tests\obj\ tools\tests\paste_file_uri_test.cpp src\PasteFile.cpp src\Base64.cpp /I src shlwapi.lib shell32.lib ole32.lib gdiplus.lib
IF !ERRORLEVEL! NEQ 0 ( ECHO [ERROR] paste_file_uri_test compilation failed & SET "RC=1" ) ELSE (
  ".target\tests\paste_file_uri_test.exe"
  IF !ERRORLEVEL! NEQ 0 SET "RC=1"
)

CL /nologo /std:c++17 /EHsc /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /Fe:.target\tests\paste_image_test.exe /Fo:.target\tests\obj\ tools\tests\paste_image_test.cpp src\PasteImage.cpp src\Base64.cpp /I src gdi32.lib user32.lib kernel32.lib gdiplus.lib shlwapi.lib
IF !ERRORLEVEL! NEQ 0 ( ECHO [ERROR] paste_image_test compilation failed & SET "RC=1" ) ELSE (
  ".target\tests\paste_image_test.exe"
  IF !ERRORLEVEL! NEQ 0 SET "RC=1"
)

CL /nologo /std:c++17 /EHsc /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /Fe:.target\tests\base64_test.exe /Fo:.target\tests\obj\ tools\tests\base64_test.cpp src\Base64.cpp /I src
IF !ERRORLEVEL! NEQ 0 ( ECHO [ERROR] base64_test compilation failed & SET "RC=1" ) ELSE (
  ".target\tests\base64_test.exe"
  IF !ERRORLEVEL! NEQ 0 SET "RC=1"
)

CL /nologo /std:c++17 /EHsc /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /Fe:.target\tests\scroll_auto_test.exe /Fo:.target\tests\obj\ tools\tests\scroll_auto_test.cpp /I src /I scintilla\include user32.lib comctl32.lib
IF !ERRORLEVEL! NEQ 0 ( ECHO [ERROR] scroll_auto_test compilation failed & SET "RC=1" ) ELSE (
  ".target\tests\scroll_auto_test.exe"
  IF !ERRORLEVEL! NEQ 0 SET "RC=1"
)

CL /nologo /std:c++17 /EHsc /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /Fe:.target\tests\replacer_acl_test.exe /Fo:.target\tests\obj\ tools\tests\replacer_acl_test.cpp tools\notepad420-replacer\AclGuard.cpp shlwapi.lib shell32.lib ole32.lib oleaut32.lib advapi32.lib user32.lib
IF !ERRORLEVEL! NEQ 0 ( ECHO [ERROR] replacer_acl_test compilation failed & SET "RC=1" ) ELSE (
  ".target\tests\replacer_acl_test.exe"
  IF !ERRORLEVEL! NEQ 0 SET "RC=1"
)

CL /nologo /std:c++17 /EHsc /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /Fe:.target\tests\replacer_hash_test.exe /Fo:.target\tests\obj\ tools\tests\replacer_hash_test.cpp tools\notepad420-replacer\DefaultApp.cpp advapi32.lib ole32.lib shell32.lib
IF !ERRORLEVEL! NEQ 0 ( ECHO [ERROR] replacer_hash_test compilation failed & SET "RC=1" ) ELSE (
  ".target\tests\replacer_hash_test.exe"
  IF !ERRORLEVEL! NEQ 0 SET "RC=1"
)

CL /nologo /std:c++17 /EHsc /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /Fe:.target\tests\replacer_cli_test.exe /Fo:.target\tests\obj\ tools\tests\replacer_cli_test.cpp tools\notepad420-replacer\ReplacerOps.cpp tools\notepad420-replacer\AclGuard.cpp tools\notepad420-replacer\DefaultApp.cpp /I tools\notepad420-replacer /I src shlwapi.lib advapi32.lib user32.lib ole32.lib shell32.lib
IF !ERRORLEVEL! NEQ 0 ( ECHO [ERROR] replacer_cli_test compilation failed & SET "RC=1" ) ELSE (
  ".target\tests\replacer_cli_test.exe"
  IF !ERRORLEVEL! NEQ 0 SET "RC=1"
)

CL /nologo /std:c++17 /EHsc /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /Fe:.target\tests\paste_bridge_test.exe /Fo:.target\tests\obj\ tools\tests\paste_bridge_test.cpp src\KeditBridge.cpp src\Base64.cpp /I src
IF !ERRORLEVEL! NEQ 0 ( ECHO [ERROR] paste_bridge_test compilation failed & SET "RC=1" ) ELSE (
  ".target\tests\paste_bridge_test.exe"
  IF !ERRORLEVEL! NEQ 0 SET "RC=1"
)

CL /nologo /std:c++17 /EHsc /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /Fe:.target\tests\edit_cjk_mask_test.exe /Fo:.target\tests\obj\ tools\tests\edit_cjk_mask_test.cpp
IF !ERRORLEVEL! NEQ 0 ( ECHO [ERROR] edit_cjk_mask_test compilation failed & SET "RC=1" ) ELSE (
  ".target\tests\edit_cjk_mask_test.exe"
  IF !ERRORLEVEL! NEQ 0 SET "RC=1"
)

IF !RC! NEQ 0 ( ECHO [FAIL] unit tests failed ) ELSE ( ECHO [OK] unit tests passed )
ENDLOCAL & EXIT /B %RC%
