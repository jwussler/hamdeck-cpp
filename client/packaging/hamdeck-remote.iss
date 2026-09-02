; Standalone installer for the HamDeck Remote client.
;
; ⚠️ THIS EXISTS BECAUSE A ZIP IS NOT A DELIVERABLE. The client had been shipping
; only inside the pusher's package, so the release page never offered a Windows
; client at all - and the stopgap was a zip, which asks the person downloading it
; to find an exe among 1,369 files and make their own shortcut. Nobody does that.
; Every platform gets an installer: .dmg on Mac, .deb on Linux, this on Windows.
;
; ⚠️ IT INSTALLS PER-USER (no admin prompt) into LocalAppData, deliberately. The
; combined HamDeck-win-Setup.exe is a Velopack package that installs to the same
; kind of location; asking for elevation to run a radio panel is friction with
; nothing behind it.
;
; ⚠️ NO AUTO-UPDATE HERE, and that is the honest tradeoff. Velopack's updater
; belongs to the combined package. This installs a fixed version; the notes say so.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\pusher\packaging\dist\hamdeck-pusher\client"
#endif

[Setup]
AppId={{7C4E2E31-9E0E-4E4B-9A8B-2F6C9F5D3A11}
AppName=HamDeck Remote
AppVersion={#AppVersion}
AppVerName=HamDeck Remote {#AppVersion}
AppPublisher=WA0O
AppPublisherURL=https://hamdeck.io
DefaultDirName={localappdata}\HamDeck Remote
DefaultGroupName=HamDeck
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
OutputDir=.
OutputBaseFilename=HamDeckRemote-win-Setup
SetupIconFile=..\..\packaging\icons\hamdeck.ico
UninstallDisplayIcon={app}\hamdeck-qml.exe
UninstallDisplayName=HamDeck Remote
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[Files]
; ⚠️ The whole staged tree, recursively. The client is useless without its Qt
; runtime - platforms\qwindows.dll in particular, whose absence produces "This
; application failed to start" with no clue which file is missing.
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\HamDeck Remote"; Filename: "{app}\hamdeck-qml.exe"
Name: "{userdesktop}\HamDeck Remote"; Filename: "{app}\hamdeck-qml.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"

[Run]
Filename: "{app}\hamdeck-qml.exe"; Description: "Start HamDeck Remote"; Flags: nowait postinstall skipifsilent
