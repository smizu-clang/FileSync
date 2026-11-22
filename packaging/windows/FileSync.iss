#ifndef AppVersion
  #define AppVersion "1.0.1"
#endif

#ifndef StageDir
  #define StageDir "..\..\dist"
#endif

[Setup]
AppId={{E6E02C9D-783E-4E2A-95C1-091C0E5D1793}
AppName=FileSync
AppVersion={#AppVersion}
AppPublisher=smizu-clang
DefaultDirName={autopf}\FileSync
DefaultGroupName=FileSync
PrivilegesRequired=admin
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
OutputBaseFilename=FileSync-{#AppVersion}-x64-setup
UninstallDisplayIcon={app}\FileSync.exe
WizardStyle=modern
DisableProgramGroupPage=yes
Compression=lzma2/ultra64
SolidCompression=yes

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\FileSync"; Filename: "{app}\FileSync.exe"
Name: "{autodesktop}\FileSync"; Filename: "{app}\FileSync.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Run]
Filename: "{app}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing Microsoft Visual C++ Runtime..."; Flags: waituntilterminated
Filename: "{app}\FileSync.exe"; Description: "Launch FileSync"; Flags: postinstall nowait skipifsilent
