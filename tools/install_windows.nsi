; GlorpDrums Windows NSIS Installer
; Builds an installer for GlorpDrums VST3 plugin and standalone apps

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "WinCore.nsh"
!include "x64.nsh"

;==============================================================================
; General Settings
;==============================================================================
Name "GlorpDrums"
OutFile "GlorpDrums-Installer.exe"
InstallDir "$PROGRAMFILES64\GlorpDrums"
InstallDirRegKey HKCU "Software\GlorpDrums" "InstallDir"
RequestExecutionLevel admin
Unicode true

;==============================================================================
; Pages
;==============================================================================
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "LICENSE.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

;==============================================================================
; Languages
;==============================================================================
!insertmacro MUI_LANGUAGE "English"

;==============================================================================
; Variables
;==============================================================================
Var VST3_PATH
Var StartMenuFolder

;==============================================================================
; Functions
;==============================================================================
Function .onInit
    ; Check if running on 64-bit Windows
    ${IfNot} ${RunningX64}
        MessageBox MB_ICONSTOP "This installer requires 64-bit Windows."
        Abort
    ${EndIf}

    ; Set default VST3 path
    StrCpy $VST3_PATH "$PROGRAMFILES64\Common Files\VST3"
FunctionEnd

Function WritePluginPaths
    ; Write VST3 path to registry (for hosts that check HKCU)
    WriteRegStr HKCU "Software\VST\VSTPluginsPath" "" $VST3_PATH
    WriteRegStr HKLM "Software\VST\VSTPluginsPath" "" $VST3_PATH
FunctionEnd

Function CreateShortcuts
    ; Create Start Menu folder
    CreateDirectory "$SMPROGRAMS\GlorpDrums"

    ; GlorpDrums standalone shortcut
    CreateShortCut "$SMPROGRAMS\GlorpDrums\GlorpDrums.lnk" "$INSTDIR\bin\GlorpDrums.exe" "" "$INSTDIR\icons\glorpdrums_icon.ico" 0

    ; GlorpKit Studio shortcut
    CreateShortCut "$SMPROGRAMS\GlorpDrums\GlorpKit Studio.lnk" "$INSTDIR\bin\GlorpKitEditor.exe" "" "$INSTDIR\icons\glorpkit_studio_icon.ico" 0

    ; Uninstall shortcut
    CreateShortCut "$SMPROGRAMS\GlorpDrums\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
FunctionEnd

Function RemoveShortcuts
    ; Remove Start Menu folder
    RMDir /r "$SMPROGRAMS\GlorpDrums"
FunctionEnd

;==============================================================================
; Sections
;==============================================================================
Section "Main" SEC_MAIN
    SectionIn RO

    ; Set output path
    SetOutPath "$INSTDIR"

    ; Copy VST3 plugin
    SetOutPath "$INSTDIR\VST3"
    File /r "VST3\GlorpDrums.vst3\*"

    ; Copy standalone binaries
    SetOutPath "$INSTDIR\bin"
    File "Standalone\GlorpDrums.exe"
    File "GlorpKit Studio\GlorpKitEditor.exe"

    ; Copy icons
    SetOutPath "$INSTDIR\icons"
    File "icons\glorpdrums_icon.ico"
    File "icons\glorpkit_studio_icon.ico"

    ; Copy demo kits
    SetOutPath "$INSTDIR\kits"
    File /r "kits\*"

    ; Write plugin paths to registry
    Call WritePluginPaths

    ; Create Start Menu shortcuts
    Call CreateShortcuts

    ; Write uninstaller
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    ; Write install location to registry
    WriteRegStr HKCU "Software\GlorpDrums" "InstallDir" "$INSTDIR"
    WriteRegStr HKLM "Software\GlorpDrums" "InstallDir" "$INSTDIR"
SectionEnd

Section "VST3 System Path" SEC_VST3_SYSTEM
    ; Copy VST3 to system-wide location (requires admin)
    SetOutPath "$VST3_PATH"
    File /r "VST3\GlorpDrums.vst3\*"
SectionEnd

;==============================================================================
; Uninstaller
;==============================================================================
Section "Uninstall"
    ; Remove plugin paths from registry
    DeleteRegKey HKCU "Software\VST\VSTPluginsPath"
    DeleteRegKey HKLM "Software\VST\VSTPluginsPath"

    ; Remove Start Menu shortcuts
    Call RemoveShortcuts

    ; Remove installed files
    RMDir /r "$INSTDIR\VST3"
    RMDir /r "$INSTDIR\bin"
    RMDir /r "$INSTDIR\icons"
    RMDir /r "$INSTDIR\kits"

    ; Remove uninstaller
    Delete "$INSTDIR\Uninstall.exe"

    ; Remove install directory if empty
    RMDir "$INSTDIR"

    ; Remove registry keys
    DeleteRegKey HKCU "Software\GlorpDrums"
    DeleteRegKey HKLM "Software\GlorpDrums"
SectionEnd

;==============================================================================
; Descriptions
;==============================================================================
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SEC_MAIN} "Installs GlorpDrums VST3 plugin and standalone applications."
    !insertmacro MUI_DESCRIPTION_TEXT ${SEC_VST3_SYSTEM} "Also installs VST3 plugin to system-wide location ($PROGRAMFILES64\Common Files\VST3)."
!insertmacro MUI_FUNCTION_DESCRIPTION_END