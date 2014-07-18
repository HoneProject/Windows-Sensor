; Inno Setup script to build the installer for the Hone (Host-Network)
; Packet-Process Correlator for Windows
;
; Copyright (c) 2014 Battelle Memorial Institute
; Licensed under a modification of the 3-clause BSD license
; See License.txt for the full text of the license and additional disclaimers

#include "version.ini"
#define MyAppId        "{AEFB68F6-C287-4B8D-B071-C83E0F98B0BB}"
#define MyAppName      "Hone"
#define MyAppPublisher "Pacific Northwest National Laboratory"
#define MyAppURL       "http://www.pnl.gov/"

[Setup]
AppComments=Hone (Host-Network) Packet-Process Correlator for Windows
AppId={{#MyAppId}
AppName={#MyAppName}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
AppVersion={#MyAppVersion}
ArchitecturesAllowed=x86 x64
ArchitecturesInstallIn64BitMode=x64
Compression=lzma2
DefaultDirName={pf}\PNNL\{#MyAppName}
DefaultGroupName=PNNL\{#MyAppName}
DisableDirPage=yes
DisableProgramGroupPage=yes
LicenseFile=License.rtf
MinVersion=6.1
OutputBaseFilename=Hone
SetupIconFile=hone.ico
SolidCompression=yes
UninstallDisplayIcon={app}\hone.ico
VersionInfoVersion={#MyAppVersion}
WizardImageFile=hone_164x314.bmp
WizardSmallImageFile=hone_55x58.bmp

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "License.txt";  DestDir: "{app}"
Source: "Readme.html";  DestDir: "{app}"
Source: "AdminCmd.js";  DestDir: "{app}"
Source: "cmd.ico";      DestDir: "{app}"
Source: "hone.ico";     DestDir: "{app}"
Source: "trash.ico";    DestDir: "{app}"
Source: "honeutil.exe"; DestDir: "{app}"

Source: "x64\hone.sys"; DestDir: "{sys}\drivers"; Flags: 64bit; Check: Is64BitInstallMode
Source: "x86\hone.sys"; DestDir: "{sys}\drivers"; Flags: 32bit; Check: "not Is64BitInstallMode"

[Icons]
Name: "{group}\Readme";                      WorkingDir: "{app}"; Comment: "View Hone Documentation";  Filename: "{app}\Readme.html"
Name: "{group}\{#MyAppName} Reader";         WorkingDir: "{app}"; Comment: "Run Hone Reader Utility";  Filename: "{app}\honeutil.exe"; Parameters: "read -p -v -d %TEMP%"; IconFilename: "{app}\hone.ico"
Name: "{group}\{#MyAppName} Command Prompt"; WorkingDir: "{app}"; Comment: "Open Hone Command Prompt"; Filename: "{app}\AdminCmd.js";                                      IconFilename: "{app}\cmd.ico"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}";                                                    Filename: "{uninstallexe}";                                         IconFilename: "{app}\trash.ico"

[Run]
Filename: "{sys}\sc.exe";       Flags: runhidden; Parameters: "create Hone binpath= ""{sys}\drivers\hone.sys"" type= kernel start= boot error= normal"
Filename: "{app}\honeutil.exe"; Flags: runhidden; Parameters: "install"
Filename: "{sys}\sc.exe";       Flags: runhidden; Parameters: "start Hone"
Filename: "{app}\honeutil.exe"; Flags: runhidden; Parameters: "send-conns"

[UninstallRun]
Filename: "{sys}\sc.exe";       Flags: runhidden; Parameters: "stop Hone"
Filename: "{app}\honeutil.exe"; Flags: runhidden; Parameters: "uninstall"
Filename: "{sys}\sc.exe";       Flags: runhidden; Parameters: "delete Hone"

[Code]
// Adapted from http://www.lextm.com/2007/08/inno-setup-script-sample-for-version-comparison-2/
function GetNumber(var temp: String): Integer;
var
	part: String;
	pos1: Integer;
begin
	if Length(temp) = 0 then
	begin
		Result := -1;
		Exit;
	end;
	pos1 := Pos('.', temp);
	if (pos1 = 0) then
	begin
		Result := StrToInt(temp);
		temp := '';
	end
	else
	begin
		part := Copy(temp, 1, pos1 - 1);
		temp := Copy(temp, pos1 + 1, Length(temp));
		Result := StrToInt(part);
	end;
end;

function CompareInner(var temp1, temp2: String): Integer;
var
	num1, num2: Integer;
begin
	num1 := GetNumber(temp1);
	num2 := GetNumber(temp2);
	if (num1 = -1) or (num2 = -1) then
	begin
		Result := 0;
		Exit;
	end;
	if (num1 > num2) then
	begin
		Result := 1;
	end
	else if (num1 < num2) then
	begin
		Result := -1;
	end
	else
	begin
		Result := CompareInner(temp1, temp2);
	end;
end;

function CompareVersion(str1, str2: String): Integer;
var
	temp1, temp2: String;
begin
	temp1 := str1;
	temp2 := str2;
	Result := CompareInner(temp1, temp2);
end;

// Check if newer version is already installed
function InitializeSetup(): Boolean;
var
	oldVersion:     String;
	versionCompare: Integer;
begin
	Result := True;
	if RegKeyExists(HKEY_LOCAL_MACHINE, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{#MyAppId}_is1') then
	begin
		RegQueryStringValue(HKEY_LOCAL_MACHINE, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{#MyAppId}_is1', 'DisplayVersion', oldVersion);
		versionCompare := CompareVersion(oldVersion, '{#MyAppVersion}');
		if (versionCompare < 0) then
		begin
			if (WizardSilent = False) and (MsgBox('{#MyAppName}' + oldVersion + ' is installed. Do you want to upgrade to {#MyAppVersion}?', mbConfirmation, MB_YESNO) = IDNO) then
			begin
				Result := False;
			end;
		end
		else if (versionCompare > 0) then
		begin
			if (WizardSilent = False) then
			begin
				MsgBox('{#MyAppName}' + oldVersion + ' is newer than {#MyAppVersion}. The installer will now exit.', mbInformation, MB_OK);
			end;
			Result := False;
		end
		else
		begin
			if (WizardSilent = False) and (MsgBox('{#MyAppName}' + oldVersion + ' is already installed. Do you want to reinstall?', mbConfirmation, MB_YESNO) = IDNO) then
			begin
				Result := False;
			end;
		end;
	end;
end;

// Stop the driver service before upgrading, since it may have locks on files that need to be replaced
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
	resultCode: Integer;
begin
	Exec(ExpandConstant('{sys}\sc.exe'), 'stop Hone', '', SW_HIDE, ewWaitUntilTerminated, resultCode);
	Result := '';
end;
