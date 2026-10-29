; Typist Windows installer, assembled from the staged distribution tree.
; Run: makensis /DSTAGE_DIR=stage /DVERSION=0.1.0 packaging/windows/typist.nsi

!define PRODUCT_NAME "Typist"
!define PRODUCT_VERSION "${VERSION}"
!define PRODUCT_PUBLISHER "Typist project"
!define PRODUCT_DIR_REGKEY "Software\Microsoft\Windows\CurrentVersion\App Paths\typist.exe"
!define PRODUCT_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Typist"

!include "MUI2.nsh"

Name "Typist ${VERSION} (x64)"
OutFile "Typist-${VERSION}-win64-installer.exe"
InstallDir "$PROGRAMFILES64\Typist"
InstallDirRegKey HKLM "${PRODUCT_DIR_REGKEY}" ""
ShowInstDetails show
ShowUninstDetails show
RequestExecutionLevel admin

!define MUI_ABORTWARNING
!define MUI_ICON "${__FILEDIR__}\typist.ico"
!define MUI_UNICON "${__FILEDIR__}\typist.ico"
!define MUI_WELCOMEPAGE_TITLE "Install Typist ${VERSION}"
!define MUI_WELCOMEPAGE_TEXT "Typist is a WYSIWYG Markdown editor built on Qt 6. The package ships the Qt runtime DLLs next to the binary."

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "Typist" SEC_MAIN
  SetOutPath "$INSTDIR"
  File /r "${STAGE_DIR}\*.*"
  WriteUninstaller "$INSTDIR\uninstall.exe"
  WriteRegStr HKLM "${PRODUCT_DIR_REGKEY}" "" "$INSTDIR\bin\typist.exe"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "DisplayName" "Typist"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "DisplayIcon" "$INSTDIR\bin\typist.exe,0"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "UninstallString" "$INSTDIR\uninstall.exe"
  WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "InstallLocation" "$INSTDIR"
  CreateDirectory "$SMPROGRAMS\Typist"
  CreateShortCut "$SMPROGRAMS\Typist\Typist.lnk" "$INSTDIR\bin\typist.exe"
  CreateShortCut "$DESKTOP\Typist.lnk" "$INSTDIR\bin\typist.exe"
SectionEnd

Section "Uninstall"
  Delete "$DESKTOP\Typist.lnk"
  Delete "$SMPROGRAMS\Typist\Typist.lnk"
  RMDir "$SMPROGRAMS\Typist"
  RMDir /r "$INSTDIR"
  DeleteRegKey HKLM "${PRODUCT_UNINST_KEY}"
  DeleteRegKey HKLM "${PRODUCT_DIR_REGKEY}"
SectionEnd
