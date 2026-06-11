; Inno Setup script for the Terrax Launcher.
;
; Produces a single TerraxLauncherSetup.exe that installs the launcher
; (TerraxLauncher.exe + glfw3.dll) into Program Files\Terrax by default, lets the
; user pick a different location, adds Start Menu + (optional) desktop shortcuts,
; and registers an uninstaller in Add/Remove Programs. The launcher's in-app
; self-update downloads and re-runs this installer to update itself in place
; (same AppId -> Inno detects the existing install and upgrades it).
;
; Compile (locally):
;   "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" ^
;       /DSrcDir="build\windows\Release\launcher" /DAppVersion=v0.2.0 ^
;       installer\TerraxLauncher.iss
; Output: installer\Output\TerraxLauncherSetup.exe

#ifndef SrcDir
  #define SrcDir "..\build\windows\Release\launcher"
#endif
#ifndef AppVersion
  #define AppVersion "dev"
#endif

[Setup]
; A stable AppId is what makes self-update upgrade in place instead of installing
; a second copy. Do not change it across versions.
AppId={{8B5F2C7E-2D5A-4F3B-9C1E-7A6E0B2D4F10}
AppName=Terrax Launcher
AppVersion={#AppVersion}
AppPublisher=Alli1223
AppPublisherURL=https://github.com/Alli1223/Terrax
DefaultDirName={autopf}\Terrax
DefaultGroupName=Terrax
DisableProgramGroupPage=yes
UninstallDisplayName=Terrax Launcher
UninstallDisplayIcon={app}\TerraxLauncher.exe
OutputDir=Output
OutputBaseFilename=TerraxLauncherSetup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; Install for all users into Program Files (needs admin / a UAC prompt).
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; If the launcher is running during a self-update, close it so its exe can be
; replaced, then carry on (we don't auto-restart; the [Run] entry relaunches).
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#SrcDir}\TerraxLauncher.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\glfw3.dll";          DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Terrax Launcher";           Filename: "{app}\TerraxLauncher.exe"
Name: "{group}\Uninstall Terrax Launcher"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Terrax Launcher";     Filename: "{app}\TerraxLauncher.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\TerraxLauncher.exe"; Description: "{cm:LaunchProgram,Terrax Launcher}"; Flags: nowait postinstall skipifsilent
