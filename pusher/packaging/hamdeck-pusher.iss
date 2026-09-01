; HamDeck Wavelog Pusher - Windows installer (Inno Setup).
;
; ⚠️ BUILT AND RUN ON WINDOWS, never cross-compiled. The CI job runs --selftest on the
; FROZEN exe before this script packages it, because a bundle that starts is not a bundle
; that works - PyInstaller has shipped an empty one on this fleet before.
;
; ⚠️ UNSIGNED. SmartScreen will warn on first run. Saying so is the honest position.

#define AppName    "HamDeck Wavelog Pusher"
#define AppVersion "0.1.0"
#define AppExe     "hamdeck-pusher.exe"

[Setup]
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
DefaultDirName={autopf}\HamDeckPusher
DefaultGroupName=HamDeck
UninstallDisplayIcon={app}\{#AppExe}
OutputBaseFilename=HamDeckPusher-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequiredOverridesAllowed=dialog
SetupIconFile=..\..\packaging\icons\hamdeck.ico
ArchitecturesInstallIn64BitMode=x64compatible

; ⚠️ THE UNINSTALLER IS GENERATED AT INSTALL TIME AND IS NOT COVERED BY SIGNING THE
; INSTALLER. Inno writes unins000.exe onto the target machine from a stub, so the signature
; applied to this setup.exe in CI never reaches it - and an unsigned executable appearing in
; Program Files is exactly the shape the heuristic that flagged us is trained on.
; SignedUninstaller signs that stub at compile time instead. It is a no-op unless a
; SignTool is configured, so it is safe to leave on while signing is not yet wired.
SignedUninstaller=yes

[Files]
; ⚠️ The whole onedir tree, not just the exe. A PyInstaller onedir build needs its
; _internal directory beside the binary or it dies at launch with no message.
Source: "dist\hamdeck-pusher\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\HamDeck Wavelog Pusher"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\HamDeck Wavelog Pusher"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon
; ⚠️ Startup is a TASK, not a default. Something that logs to a public database on every
; boot is a decision the operator makes, not one an installer makes for them.
Name: "{userstartup}\HamDeck Wavelog Pusher"; Filename: "{app}\{#AppExe}"; Tasks: startup

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"
Name: "startup"; Description: "Start automatically when I sign in"; GroupDescription: "Startup:"; Flags: unchecked

[Run]
Filename: "{app}\{#AppExe}"; Description: "Start the pusher"; Flags: nowait postinstall skipifsilent

; ⚠️ Settings live in %APPDATA%\HamDeckPusher and are NEVER touched here. The installer
; writes only into Program Files, so an update cannot overwrite the operator's API key -
; the same rule the .NET client settled on.
