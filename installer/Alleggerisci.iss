; Installatore di Alleggerisci per Windows (Inno Setup 7).
;
; Si compila con scripts/build-installer.sh, che passa:
;   /DVersione=0.1.0               versione (da CMakeLists.txt)
;   /DCartella=<dist\Alleggerisci>  cartella preparata con "cmake --install"
;   /O<cartella di uscita>
;
; Installazione per l'utente, senza diritti di amministratore (si può scegliere "per tutti gli utenti"). Nessuna
; associazione di file. L'installatore non è firmato digitalmente: Windows può mostrare un avviso.
; Chi non vuole installare usa la versione portabile (zip).

#ifndef Versione
  #error Manca /DVersione
#endif
#ifndef Cartella
  #error Manca /DCartella
#endif

[Setup]
AppId={{7B0E5C1D-6B1E-4B8E-9C55-3A2F3D6E8A41}
AppName=Alleggerisci
AppVersion={#Versione}
AppVerName=Alleggerisci {#Versione}
AppPublisher=SamuDiPi
AppPublisherURL=https://github.com/SamuDiPi/TinyImage
AppSupportURL=https://github.com/SamuDiPi/TinyImage/issues
AppUpdatesURL=https://github.com/SamuDiPi/TinyImage/releases
AppCopyright=Copyright (C) 2026 SamuDiPi - GNU GPL v3
VersionInfoVersion={#Versione}
VersionInfoDescription=Installazione di Alleggerisci
DefaultDirName={autopf}\Alleggerisci
AppendDefaultDirName=yes
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile={#Cartella}\LICENSE.txt
InfoBeforeFile=prima-di-installare.txt
OutputBaseFilename=Alleggerisci-{#Versione}-installazione
UninstallDisplayIcon={app}\Alleggerisci.exe
SetupIconFile=..\resources\alleggerisci.ico
UninstallDisplayName=Alleggerisci {#Versione}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
ChangesAssociations=no
ChangesEnvironment=no
UsedUserAreasWarning=no

[Languages]
Name: "it"; MessagesFile: "compiler:Languages\Italian.isl"

[Tasks]
Name: "inviaa"; Description: "Aggiungi ""Alleggerisci"" al menu Invia a (clic destro sulle immagini)"
Name: "desktop"; Description: "Collegamento sul desktop"; Flags: unchecked

[Files]
Source: "{#Cartella}\*"; DestDir: "{app}"; Excludes: "portabile.ini"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Alleggerisci"; Filename: "{app}\Alleggerisci.exe"
Name: "{userdesktop}\Alleggerisci"; Filename: "{app}\Alleggerisci.exe"; Tasks: desktop
Name: "{usersendto}\Alleggerisci"; Filename: "{app}\Alleggerisci.exe"; Tasks: inviaa

[Run]
Filename: "{app}\Alleggerisci.exe"; Description: "Avvia Alleggerisci"; Flags: nowait postinstall skipifsilent runasoriginaluser

[Code]
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  // Le scelte dell'utente (qualità, cartella usata per ultima, posizione della finestra): si tolgono solo se lo chiede.
  if (CurUninstallStep = usPostUninstall) and not UninstallSilent then
    if MsgBox('Eliminare anche le impostazioni di Alleggerisci (scelte di qualità, ultima cartella usata)?' + #13#10#13#10 +
              'Le immagini non vengono toccate.', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
      RegDeleteKeyIncludingSubkeys(HKCU, 'Software\Alleggerisci');
end;
