; Inno Setup 6: packs Pressvppp.exe into one Pressvppp_Setup.exe.
; Per-user install (no administrator rights), Windows 10/11 x64.
#define AppName "Pressvppp"
#ifndef AppVersion
  #define AppVersion "2.0.0"
#endif

[Setup]
AppId={{6B2C1E4A-7F3D-4C8E-9A51-0D2E6F8B3C71}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Pressvppp
DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=yes
PrivilegesRequired=lowest
OutputDir=..\Output
OutputBaseFilename=Pressvppp_Setup
SetupIconFile=..\res\icon.ico
UninstallDisplayIcon={app}\Pressvppp.exe
UninstallDisplayName={#AppName} — изменение голоса
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
CloseApplications=force
RestartApplications=no

[Languages]
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "autostart"; Description: "Запускать Pressvppp вместе с Windows (в трее)"; GroupDescription: "Дополнительно:"; Flags: unchecked

[Files]
Source: "..\build\Release\Pressvppp.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\Pressvppp.exe"
Name: "{group}\Удалить {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\Pressvppp.exe"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Pressvppp"; ValueData: """{app}\Pressvppp.exe"" --minimized"; Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{app}\Pressvppp.exe"; Description: "Запустить Pressvppp"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{sys}\taskkill.exe"; Parameters: "/IM Pressvppp.exe /F"; Flags: runhidden; RunOnceId: "KillPressvppp"

[UninstallDelete]
Type: filesandordirs; Name: "{app}"
