; Iro Color Picker installer (Inno Setup 6).
;
; Per-user install without administrator rights. "Start with Windows" is
; enabled on a fresh install only; upgrades keep whatever the user chose.
; Uninstalling removes the startup entry, the settings and the recent colors.
;
; Built by build.ps1, which passes /DBuildDir=<folder with Iro.exe>.

#define AppVersion "1.0.0"
#define AppName "Iro Color Picker"
#define RunKey "Software\Microsoft\Windows\CurrentVersion\Run"
#define StartupApprovedKey "Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run"

#ifndef BuildDir
  #define BuildDir "..\build\app"
#endif

[Setup]
AppId={{4C9D2E71-8B3A-4F05-A6E2-1D7B9C3F5A80}
AppName={#AppName}
AppVerName={#AppName} {#AppVersion}
AppVersion={#AppVersion}
AppPublisher=VitomDev
AppPublisherURL=https://github.com/VitomDev/Iro-Color-Picker
AppSupportURL=https://github.com/VitomDev/Iro-Color-Picker/issues
AppCopyright=Copyright (c) 2026 VitomDev
VersionInfoVersion={#AppVersion}
VersionInfoCompany=VitomDev
VersionInfoDescription={#AppName} - Screen color picker Setup
DefaultDirName={localappdata}\Programs\{#AppName}
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputBaseFilename=IroSetup
SetupIconFile=..\assets\iro.ico
UninstallDisplayIcon={app}\Iro.exe
UninstallDisplayName={#AppName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; Iro is closed by the code below, through its own window.
CloseApplications=no

[Files]
Source: "{#BuildDir}\Iro.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\Iro.exe"

[Run]
; The usual "Launch" checkbox on the last page, checked by default. Iro
; starts once Finish is clicked; silent upgrades start it as well.
Filename: "{app}\Iro.exe"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent
Filename: "{app}\Iro.exe"; Flags: nowait; Check: WizardSilent

[UninstallDelete]
Type: filesandordirs; Name: "{localappdata}\{#AppName}"

[Code]
var
  IsUpgrade: Boolean;

// Iro closes cleanly (tray icon, shortcuts) when its window gets WM_CLOSE.
procedure CloseRunningIro;
var
  Window: HWND;
  Attempts: Integer;
begin
  Window := FindWindowByClassName('IroColorPickerTray');
  if Window = 0 then Exit;
  PostMessage(Window, 16 { WM_CLOSE }, 0, 0);
  Attempts := 0;
  while (FindWindowByClassName('IroColorPickerTray') <> 0) and (Attempts < 50) do
  begin
    Sleep(100);
    Attempts := Attempts + 1;
  end;
  // Give the process a moment to exit after its window is gone.
  Sleep(300);
end;

function InitializeSetup: Boolean;
begin
  IsUpgrade := RegKeyExists(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{#SetupSetting("AppId")}_is1');
  Result := True;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  CloseRunningIro;
  Result := '';
end;

function AutostartCommand: String;
begin
  Result := '"' + ExpandConstant('{app}\Iro.exe') + '" --background';
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    if not IsUpgrade then
      // Fresh install: Start with Windows is on by default.
      RegWriteStringValue(HKCU, '{#RunKey}', '{#AppName}', AutostartCommand)
    else if RegValueExists(HKCU, '{#RunKey}', '{#AppName}') then
      // Upgrade: keep the user's choice, only refresh the path.
      RegWriteStringValue(HKCU, '{#RunKey}', '{#AppName}', AutostartCommand);
  end;
end;

function InitializeUninstall: Boolean;
begin
  CloseRunningIro;
  Result := True;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    RegDeleteValue(HKCU, '{#RunKey}', '{#AppName}');
    RegDeleteValue(HKCU, '{#StartupApprovedKey}', '{#AppName}');
    RegDeleteKeyIncludingSubkeys(HKCU, 'Software\{#AppName}');
  end;
end;
