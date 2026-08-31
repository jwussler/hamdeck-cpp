; HamDeck client — Windows installer (Inno Setup).
;
; ⚠️ THIS IS BUILT ON WINDOWS, NOT CROSS-COMPILED. Qt does not cross-compile
; comfortably, and more importantly a Windows binary that has never run on
; Windows is exactly the release the .NET client shipped that could not launch at
; all while every test passed (CARRYOVER.md section 8). The CI job runs the
; binary on the runner before packaging it.
;
; ⚠️ UNSIGNED. Windows SmartScreen will warn on first run. Saying so is the
; honest position - BRAND.md's voice section: name what does not work.

#define AppName    "HamDeck"
#define AppVersion "0.1.7"
#define AppExe     "hamdeck-qml.exe"

[Setup]
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion} (pre-release)
DefaultDirName={autopf}\HamDeck
DefaultGroupName=HamDeck
UninstallDisplayIcon={app}\{#AppExe}
OutputBaseFilename=HamDeck-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; Per-user by default: no elevation prompt, and nothing lands outside the
; operator's own profile.
PrivilegesRequiredOverridesAllowed=dialog
SetupIconFile=icons\hamdeck.ico
ArchitecturesInstallIn64BitMode=x64compatible

[Files]
; windeployqt has already staged the Qt runtime, QML modules and plugins into
; this directory. ⚠️ Copying only the .exe produces an installer that fails at
; launch with no message - Qt apps need their platform plugin and QML modules
; beside them.
Source: "deploy\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\HamDeck"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\HamDeck"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"

[Run]
Filename: "{app}\{#AppExe}"; Description: "Start HamDeck"; Flags: nowait postinstall skipifsilent

[Messages]
; Say what it is, up front, rather than in a README nobody opens.
WelcomeLabel2=This is a PRE-RELEASE build of HamDeck.%n%nIt has been tested against one radio, and the installer is unsigned - Windows will warn you on first run.%n%nHamDeck controls a transmitter. Check your settings before you key up.
