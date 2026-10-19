Unicode true
!include "MUI2.nsh"
!include "x64.nsh"
!include "WinVer.nsh"
!define APPNAME "LiDE 25 离线扫描驱动"
!define APPKEY "Software\LiDE25OpenDriver"
!define UNINSTALLKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\LiDE25OpenDriver"
Name "${APPNAME} 0.2.0 候选测试版"
OutFile "..\release\LiDE25-Win10-x64-Setup-0.2.0-rc1.exe"
InstallDir "$PROGRAMFILES64\LiDE25 Open Driver"
InstallDirRegKey HKLM "${APPKEY}" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
SetCompressorDictSize 32
CRCCheck on
ShowInstDetails show
ShowUninstDetails show
VIProductVersion "0.2.0.0"
VIAddVersionKey /LANG=2052 "ProductName" "${APPNAME}"
VIAddVersionKey /LANG=2052 "FileDescription" "LiDE 25 Windows 10 x64 Offline Setup"
VIAddVersionKey /LANG=2052 "FileVersion" "0.2.0.0"
VIAddVersionKey /LANG=2052 "LegalCopyright" "Open-source components: see included licenses"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "安装 LiDE 25 扫描驱动"
!define MUI_WELCOMEPAGE_TEXT "此安装程序用于 Windows 10 x64 和 Canon CanoScan LiDE 25。$\r$\n$\r$\n请用 USB 连接扫描仪，并解开扫描仪底部的运输锁。请先结束其他扫描任务。$\r$\n$\r$\n此候选版按离线、无旧驱动的要求设计，尚待完成全新系统验证。请先在测试电脑使用，勿作为稳定版发布。"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_TITLE "LiDE 25 驱动安装完成"
!define MUI_FINISHPAGE_TEXT "请重新打开图像处理软件以刷新扫描设备。$\r$\n$\r$\nXnView：文件 → 从扫描仪或摄像头(WIA)获取 → 导入 → 扫描。$\r$\n$\r$\n此版本提供 WIA 接口，不包含独立扫描软件。"
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR\使用说明.txt"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "查看使用说明"
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH
!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"

Function .onInit
 ${IfNot} ${RunningX64}
  MessageBox MB_ICONSTOP "此安装包仅支持 Windows 10 x64。"
  Abort
 ${EndIf}
 SetRegView 64
 ${IfNot} ${AtLeastWin10}
  MessageBox MB_ICONSTOP "此安装包仅支持 Windows 10 x64。"
  Abort
 ${EndIf}
FunctionEnd

Section "安装驱动" SEC_DRIVER
 SetRegView 64
 SetOutPath "$INSTDIR"
 File "..\artifacts\setup\payload\Lide25Wia.dll"
 File "..\artifacts\setup\payload\lide25-setup-helper.exe"
 File /oname=LICENSE.txt "..\LICENSE"
 File /oname=使用说明.txt "offline-readme.txt"
 File /r "..\artifacts\setup\payload\licenses"
 WriteUninstaller "$INSTDIR\Uninstall.exe"
 DetailPrint "正在检查扫描仪并安装离线驱动，请稍候……"
 nsExec::ExecToLog '"$INSTDIR\lide25-setup-helper.exe" --install'
 Pop $0
 ${If} $0 == 3010
  SetRebootFlag true
 ${ElseIf} $0 != 0
  ReadINIStr $1 "$INSTDIR\setup-result.ini" "Result" "Message"
  MessageBox MB_ICONSTOP "安装未完成。$\r$\n$1$\r$\n$\r$\n恢复记录保存在安装文件夹中。请勿删除该文件夹中的 setup-state.json。"
  SetErrorLevel 1
  Abort
 ${EndIf}
 WriteRegStr HKLM "${APPKEY}" "InstallDir" "$INSTDIR"
 WriteRegStr HKLM "${APPKEY}" "Version" "0.2.0-rc1"
 WriteRegStr HKLM "${UNINSTALLKEY}" "DisplayName" "${APPNAME}"
 WriteRegStr HKLM "${UNINSTALLKEY}" "DisplayVersion" "0.2.0-rc1"
 WriteRegStr HKLM "${UNINSTALLKEY}" "Publisher" "LiDE25 Open Driver Project"
 WriteRegStr HKLM "${UNINSTALLKEY}" "InstallLocation" "$INSTDIR"
 WriteRegStr HKLM "${UNINSTALLKEY}" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
 WriteRegStr HKLM "${UNINSTALLKEY}" "QuietUninstallString" '$\"$INSTDIR\Uninstall.exe$\" /S'
 WriteRegDWORD HKLM "${UNINSTALLKEY}" "NoModify" 1
 WriteRegDWORD HKLM "${UNINSTALLKEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
 SetRegView 64
 DetailPrint "正在恢复扫描仪原来的设置……"
 nsExec::ExecToLog '"$INSTDIR\lide25-setup-helper.exe" --uninstall'
 Pop $0
 ${If} $0 != 0
  ReadINIStr $1 "$INSTDIR\setup-result.ini" "Result" "Message"
  MessageBox MB_ICONSTOP "卸载暂未完成。$\r$\n$1$\r$\n$\r$\n请连接安装时使用的扫描仪，并结束扫描后重试。原设置备份已保留。"
  SetErrorLevel 1
  Abort
 ${EndIf}
 DeleteRegKey HKLM "${UNINSTALLKEY}"
 DeleteRegKey HKLM "${APPKEY}"
 Delete /REBOOTOK "$INSTDIR\Lide25Wia.dll"
 Delete /REBOOTOK "$INSTDIR\lide25-setup-helper.exe"
 Delete "$INSTDIR\LICENSE.txt"
 Delete "$INSTDIR\使用说明.txt"
 Delete "$INSTDIR\Uninstall.exe"
 ; Keep setup-state.json and logs: they are recovery records, not user scans.
 ; Do not recursively remove an installation directory chosen by the user.
 DetailPrint "驱动已卸载。恢复记录保留在原安装文件夹中。"
SectionEnd
