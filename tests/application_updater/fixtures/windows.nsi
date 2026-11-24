; Disposable installer fixture for windows_native_tests.ps1.
Unicode true
!include "MUI2.nsh"
!include "FileFunc.nsh"
!ifndef APPDIR
!error "APPDIR must contain the fixture application tree"
!endif
!ifndef OUTFILE
!error "OUTFILE must name the fixture installer"
!endif
Name "Bambu Studio update test fixture"
OutFile "${OUTFILE}"
RequestExecutionLevel user
SetCompressor /SOLID lzma
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_LANGUAGE "English"
Function .onInit
  ${GetParameters} $R0
  ClearErrors
  ${GetOptions} $R0 "/UPDATE" $R1
  IfErrors refuse
  IfFileExists "$INSTDIR\.bambu-updater-fixture" ready refuse
  refuse:
  SetErrorLevel 2
  Abort
  ready:
FunctionEnd
Section "Fixture"
  SetOutPath "$INSTDIR"
  File /r "${APPDIR}\*"
SectionEnd
