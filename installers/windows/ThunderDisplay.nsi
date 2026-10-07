Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "nsDialogs.nsh"
!include "WinVer.nsh"
!include "x64.nsh"

!ifndef TD_VERSION
  !error "Supply TD_VERSION, TD_SOURCE, TD_ROOT and TD_OUTPUT"
!endif
!define APPKEY "Software\ThunderDisplay\Installer"
!define UNINSTALLKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\ThunderDisplay"
Name "ThunderDisplay"
OutFile "${TD_OUTPUT}"
InstallDir "$LOCALAPPDATA\Programs\ThunderDisplay"
InstallDirRegKey HKCU "${APPKEY}" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
SetCompressorDictSize 32
VIProductVersion "${TD_VERSION}.0"
VIAddVersionKey /LANG=1033 "ProductName" "ThunderDisplay Setup"
VIAddVersionKey /LANG=1033 "FileVersion" "${TD_VERSION}"
VIAddVersionKey /LANG=1033 "FileDescription" "ThunderDisplay Installer"
VIAddVersionKey /LANG=1033 "LegalCopyright" "ThunderDisplay contributors"
!define MUI_ICON "${TD_ROOT}/windows-client/assets/ThunderDisplay.ico"
!define MUI_UNICON "${TD_ROOT}/windows-client/assets/ThunderDisplay.ico"
!define MUI_ABORTWARNING
!define MUI_LANGDLL_REGISTRY_ROOT HKCU
!define MUI_LANGDLL_REGISTRY_KEY "${APPKEY}"
!define MUI_LANGDLL_REGISTRY_VALUENAME "Language"
!define MUI_FINISHPAGE_RUN "$INSTDIR\ThunderDisplayClient.exe"
!define MUI_FINISHPAGE_RUN_PARAMETERS "--show-settings"
!define MUI_FINISHPAGE_RUN_TEXT "$(LaunchSettings)"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${TD_ROOT}/LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
Page custom OptionsPage OptionsLeave
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH
!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"

LangString LaunchSettings ${LANG_SIMPCHINESE} "打开 ThunderDisplay 设置"
LangString LaunchSettings ${LANG_ENGLISH} "Open ThunderDisplay settings"
LangString OptionsTitle ${LANG_SIMPCHINESE} "启动与快捷方式"
LangString OptionsTitle ${LANG_ENGLISH} "Startup and shortcuts"
LangString OptionsText ${LANG_SIMPCHINESE} "启动后在系统托盘后台运行。双击 ThunderDisplay 图标打开设置；右键菜单可退出。"
LangString OptionsText ${LANG_ENGLISH} "Runs in the system tray. Double-click the ThunderDisplay icon for settings; right-click to quit."
LangString LoginText ${LANG_SIMPCHINESE} "登录 Windows 后自动在托盘启动"
LangString LoginText ${LANG_ENGLISH} "Start in the tray when I sign in to Windows"
LangString DesktopText ${LANG_SIMPCHINESE} "创建桌面快捷方式"
LangString DesktopText ${LANG_ENGLISH} "Create a desktop shortcut"
LangString NeedWindows ${LANG_SIMPCHINESE} "需要 Windows 10 或更新版本（x64）。"
LangString NeedWindows ${LANG_ENGLISH} "Windows 10 or newer (x64) is required."
LangString CloseClient ${LANG_SIMPCHINESE} "ThunderDisplay 仍在运行。请从 ThunderDisplay 托盘菜单退出后重试。"
LangString CloseClient ${LANG_ENGLISH} "ThunderDisplay is still running. Quit from the ThunderDisplay tray menu, then retry."
LangString UninstallLink ${LANG_SIMPCHINESE} "卸载 ThunderDisplay"
LangString UninstallLink ${LANG_ENGLISH} "Uninstall ThunderDisplay"

Var LoginCheckbox
Var DesktopCheckbox
Var StartAtLogin
Var DesktopShortcut

Function .onInit
  SetShellVarContext current
  SetRegView 64
  !insertmacro MUI_LANGDLL_DISPLAY
  ${IfNot} ${RunningX64}
  ${OrIfNot} ${AtLeastWin10}
    MessageBox MB_ICONSTOP "$(NeedWindows)"
    Abort
  ${EndIf}
  StrCpy $StartAtLogin ${BST_CHECKED}
  StrCpy $DesktopShortcut ${BST_CHECKED}
  ReadRegStr $0 HKCU "${APPKEY}" "InstallDir"
  ${If} $0 != ""
    ReadRegDWORD $StartAtLogin HKCU "${APPKEY}" "StartAtLogin"
    ReadRegDWORD $DesktopShortcut HKCU "${APPKEY}" "DesktopShortcut"
  ${EndIf}
FunctionEnd

Function OptionsPage
  !insertmacro MUI_HEADER_TEXT "$(OptionsTitle)" "$(OptionsText)"
  nsDialogs::Create 1018
  Pop $0
  ${If} $0 == error
    Abort
  ${EndIf}
  ${NSD_CreateLabel} 0 0 100% 40u "$(OptionsText)"
  Pop $0
  ${NSD_CreateCheckbox} 0 55u 100% 15u "$(LoginText)"
  Pop $LoginCheckbox
  ${NSD_SetState} $LoginCheckbox $StartAtLogin
  ${NSD_CreateCheckbox} 0 85u 100% 15u "$(DesktopText)"
  Pop $DesktopCheckbox
  ${NSD_SetState} $DesktopCheckbox $DesktopShortcut
  nsDialogs::Show
FunctionEnd
Function OptionsLeave
  ${NSD_GetState} $LoginCheckbox $StartAtLogin
  ${NSD_GetState} $DesktopCheckbox $DesktopShortcut
FunctionEnd

!macro StopClient PREFIX
Function ${PREFIX}StopClient
  FindWindow $1 "ThunderDisplay"
  ${If} $1 != 0
    System::Call 'user32::GetWindowThreadProcessId(p r1, *i .r4)'
    System::Call 'kernel32::OpenProcess(i 0x00100000, i 0, i r4) p.r5'
    System::Call 'user32::RegisterWindowMessageW(w "ThunderDisplay.ExitForInstaller.v1") i.r0'
    SendMessage $1 $0 0 0 /TIMEOUT=2000
    ; Older portable clients exit when their setup window is closed. New
    ; clients handle the registered message; closing setup merely hides it.
    FindWindow $2 "ThunderDisplaySetup"
    ${If} $2 != 0
      SendMessage $2 ${WM_CLOSE} 0 0 /TIMEOUT=2000
    ${EndIf}
    StrCpy $3 0
    ${Do}
      FindWindow $1 "ThunderDisplay"
      ${If} $1 == 0
        ${ExitDo}
      ${EndIf}
      Sleep 100
      IntOp $3 $3 + 1
    ${LoopWhile} $3 < 100
    ${If} $5 != 0
      System::Call 'kernel32::WaitForSingleObject(p r5, i 5000) i.r6'
      System::Call 'kernel32::CloseHandle(p r5)'
      ${If} $6 != 0
        MessageBox MB_ICONSTOP "$(CloseClient)"
        Abort
      ${EndIf}
    ${EndIf}
    ${If} $1 != 0
      MessageBox MB_ICONSTOP "$(CloseClient)"
      Abort
    ${EndIf}
  ${EndIf}
FunctionEnd
!macroend
!insertmacro StopClient ""
!insertmacro StopClient "un."

Section "ThunderDisplay" Main
  Call StopClient
  SetOutPath "$INSTDIR"
  File "${TD_SOURCE}/ThunderDisplayClient.exe"
  File "${TD_SOURCE}/START-HERE.txt"
  SetOutPath "$INSTDIR\licenses"
  File /r "${TD_SOURCE}/licenses/*"
  SetOutPath "$INSTDIR"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "${APPKEY}" "InstallDir" "$INSTDIR"
  WriteRegDWORD HKCU "${APPKEY}" "StartAtLogin" $StartAtLogin
  WriteRegDWORD HKCU "${APPKEY}" "DesktopShortcut" $DesktopShortcut
  WriteRegStr HKCU "${UNINSTALLKEY}" "DisplayName" "ThunderDisplay"
  WriteRegStr HKCU "${UNINSTALLKEY}" "DisplayVersion" "${TD_VERSION}"
  WriteRegStr HKCU "${UNINSTALLKEY}" "Publisher" "ThunderDisplay"
  WriteRegStr HKCU "${UNINSTALLKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALLKEY}" "DisplayIcon" "$INSTDIR\ThunderDisplayClient.exe,0"
  WriteRegStr HKCU "${UNINSTALLKEY}" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
  WriteRegStr HKCU "${UNINSTALLKEY}" "QuietUninstallString" '$\"$INSTDIR\Uninstall.exe$\" /S'
  WriteRegDWORD HKCU "${UNINSTALLKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALLKEY}" "NoRepair" 1
  WriteRegDWORD HKCU "${UNINSTALLKEY}" "EstimatedSize" ${TD_INSTALLED_KB}
  CreateDirectory "$SMPROGRAMS\ThunderDisplay"
  CreateShortcut "$SMPROGRAMS\ThunderDisplay\ThunderDisplay.lnk" "$INSTDIR\ThunderDisplayClient.exe" "--show-settings"
  Delete "$SMPROGRAMS\ThunderDisplay\Uninstall.lnk"
  Delete "$SMPROGRAMS\ThunderDisplay\卸载 ThunderDisplay.lnk"
  Delete "$SMPROGRAMS\ThunderDisplay\Uninstall ThunderDisplay.lnk"
  CreateShortcut "$SMPROGRAMS\ThunderDisplay\$(UninstallLink).lnk" "$INSTDIR\Uninstall.exe"
  ${If} $DesktopShortcut == ${BST_CHECKED}
    CreateShortcut "$DESKTOP\ThunderDisplay.lnk" "$INSTDIR\ThunderDisplayClient.exe" "--show-settings"
  ${Else}
    Delete "$DESKTOP\ThunderDisplay.lnk"
  ${EndIf}
  ${If} $StartAtLogin == ${BST_CHECKED}
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "ThunderDisplay" '$\"$INSTDIR\ThunderDisplayClient.exe$\" --background'
  ${Else}
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "ThunderDisplay"
  ${EndIf}
SectionEnd

Function un.onInit
  SetShellVarContext current
  SetRegView 64
  !insertmacro MUI_UNGETLANGUAGE
FunctionEnd
Section "Uninstall"
  Call un.StopClient
  DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "ThunderDisplay"
  DeleteRegKey HKCU "${UNINSTALLKEY}"
  DeleteRegKey HKCU "${APPKEY}"
  Delete "$DESKTOP\ThunderDisplay.lnk"
  Delete "$SMPROGRAMS\ThunderDisplay\ThunderDisplay.lnk"
  Delete "$SMPROGRAMS\ThunderDisplay\Uninstall.lnk"
  Delete "$SMPROGRAMS\ThunderDisplay\卸载 ThunderDisplay.lnk"
  Delete "$SMPROGRAMS\ThunderDisplay\Uninstall ThunderDisplay.lnk"
  RMDir "$SMPROGRAMS\ThunderDisplay"
  Delete "$INSTDIR\ThunderDisplayClient.exe"
  Delete "$INSTDIR\START-HERE.txt"
  Delete "$INSTDIR\licenses\*.txt"
  Delete "$INSTDIR\licenses\COPYING*"
  RMDir "$INSTDIR\licenses"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  ; Leave connection preferences and diagnostic logs for a later reinstall.
SectionEnd
