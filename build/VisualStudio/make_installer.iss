; notepad420 Inno Setup script —— 批次 034 从头重写(ADR 0009/0010/0011)
; Build: build\VisualStudio\make_installer.bat → .target\dist\notepad420-setup-x64-<version>.exe
;
; 页面流:欢迎 → 选目录(总是显示,重装可改)→ 准备 → 安装 → 完成页(四枚收尾复选框)。
; 无「选择附加任务」页;入口 = 开始菜单扁平快捷方式(无条件) + 桌面图标(完成页勾选)。
; ProgId Applications\notepad420.exe 由 [Registry] 无条件写入并指向 {app},
; 重装即治愈旧路径残留(ADR 0010);卸载无条件执行 replacer --restore(ADR 0011)。
; 静默安装(/SILENT /VERYSILENT)只落文件/注册表/快捷方式,不做任何系统级收尾动作。

#define MyAppName "notepad420"
#define MyAppPublisher "Sakyvo"
#define MyAppExeName "notepad420.exe"
#define MyReplacerExe "notepad420-replacer.exe"
#define MyAppVersion GetStringFileInfo("..\..\\.target\app\notepad420.exe", "ProductVersion")

[Setup]
AppId={{7A6E2C31-8F52-4B7E-9E14-3C2D5A9F0420}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\notepad420
DisableDirPage=no
DisableProgramGroupPage=yes
OutputDir=..\..\.target\installer
OutputBaseFilename=notepad420-setup-x64-{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=admin
UninstallDisplayIcon={app}\notepad420.exe
; ProgId 写入后让 Explorer 立刻刷新关联缓存
ChangesAssociations=yes
; 中文显示优先跟随系统 UI 语言;未匹配回落 English
ShowLanguageDialog=auto

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "chinesesimp"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Files]
Source: "..\..\.target\app\notepad420.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\.target\app\locale\zh-Hans\Notepad4.dll"; DestDir: "{app}\locale\zh-Hans"; Flags: ignoreversion recursesubdirs
Source: "..\..\.target\app\notepad420.ini"; DestDir: "{app}"; Flags: onlyifdoesntexist
Source: "..\..\.target\app\License.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\.target\app\notepad420-replacer.exe"; DestDir: "{app}"; Flags: ignoreversion

[Registry]
; ADR 0010:ProgId 归安装包持有,shell\open\command 与 DefaultIcon 永远指向本次安装目录。
; 卸载时 Inno 自动回收;--restore 的 DeleteOurProgId 是双保险(zip 布局/手动场景兜底)。
Root: HKCU; Subkey: "Software\Classes\Applications\notepad420.exe"; ValueType: string; ValueData: ""; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\notepad420.exe\shell\open\command"; ValueType: string; ValueData: """{app}\notepad420.exe"" ""%1"""
Root: HKCU; Subkey: "Software\Classes\Applications\notepad420.exe\DefaultIcon"; ValueType: expandsz; ValueData: "{app}\notepad420.exe,0"

[Icons]
; 平铺开始菜单单快捷方式(无程序组文件夹)无条件;桌面图标由完成页复选框控制,
; 在 [Code] DeinitializeSetup 里按勾选创建(见下文),卸载由 [UninstallDelete] 回收。
Name: "{autoprograms}\notepad420"; Filename: "{app}\{#MyAppExeName}"

[UninstallDelete]
; 桌面 lnk 是 [Code] 建的,Inno 不跟踪,卸载兜底删除(无论当时是否勾创建过)
Type: files; Name: "{autodesktop}\notepad420.lnk"

[Code]
var
	ShortcutCheck, ReplaceCheck, AssociateCheck, EditorCheck: TNewCheckBox;
	WantShortcut, WantReplace, WantAssociate, WantEditor: Boolean;
	FinishedInstall: Boolean;

function MakeFinishCheck(top: Integer; const caption: String;
	handler: TNotifyEvent): TNewCheckBox;
begin
	Result := TNewCheckBox.Create(WizardForm);
	Result.Parent := WizardForm.FinishedPage;
	Result.Left := WizardForm.FinishedLabel.Left;
	Result.Top := top;
	Result.Width := WizardForm.FinishedPage.ClientWidth - Result.Left - ScaleX(8);
	Result.Height := ScaleY(Result.Height);
	Result.Caption := caption;
	Result.Checked := True;
	Result.OnClick := handler;
end;

procedure OnShortcutClick(Sender: TObject);
begin
	WantShortcut := ShortcutCheck.Checked;
end;

procedure OnReplaceClick(Sender: TObject);
begin
	WantReplace := ReplaceCheck.Checked;
end;

procedure OnAssociateClick(Sender: TObject);
begin
	WantAssociate := AssociateCheck.Checked;
end;

procedure OnEditorClick(Sender: TObject);
begin
	WantEditor := EditorCheck.Checked;
end;

procedure InitializeWizard;
var
	top: Integer;
begin
	// 四枚收尾复选框挂在完成页(默认全勾;批次 038:桌面 lnk 也降为可选档)。
	// 勾选状态存进变量,在 DeinitializeSetup(wizard 已销毁)里消费。
	WantShortcut := True;
	WantReplace := True;
	WantAssociate := True;
	WantEditor := True;
	top := WizardForm.FinishedLabel.Top + WizardForm.FinishedLabel.Height + ScaleY(12);
	ShortcutCheck := MakeFinishCheck(top, CustomMessage('CreateShortcutOpt'), @OnShortcutClick);
	top := top + ShortcutCheck.Height + ScaleY(6);
	ReplaceCheck := MakeFinishCheck(top, CustomMessage('ReplaceNotepadOpt'), @OnReplaceClick);
	top := top + ReplaceCheck.Height + ScaleY(6);
	AssociateCheck := MakeFinishCheck(top, CustomMessage('AssociateOpt'), @OnAssociateClick);
	top := top + AssociateCheck.Height + ScaleY(6);
	EditorCheck := MakeFinishCheck(top, CustomMessage('SetEditorOpt'), @OnEditorClick);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
	// 安装成功完成(此后取消钮已被禁用,只能走完)——DeinitializeSetup 才收尾。
	if CurStep = ssPostInstall then
		FinishedInstall := True;
end;

function RunReplacer(const params: String; var code: Integer): Boolean;
begin
	Result := Exec(ExpandConstant('{app}\{#MyReplacerExe}'), params,
	               ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, code);
end;

procedure DeinitializeSetup;
var
	code: Integer;
	failures: String;
	lnk: String;
begin
	// 只报告「完整装完且交互模式」的收尾动作;静默安装只落文件/注册表/快捷方式，
	// 系统级动作(替换/关联/环境变量)留给交互安装或手动 CLI。
	if (not FinishedInstall) or WizardSilent then
		Exit;
	failures := '';
	if WantShortcut then begin
		// 桌面 lnk 在完成页之后才能创建([Icons] 已在 ssInstall 期跑完)。
		lnk := ExpandConstant('{autodesktop}\notepad420.lnk');
		try
			CreateShellLink(lnk, 'notepad420', ExpandConstant('{app}\{#MyAppExeName}'),
				'', ExpandConstant('{app}'), '', 0, SW_SHOWNORMAL);
		except
		end;
		if not FileExists(lnk) then
			failures := failures + '- ' + CustomMessage('CreateShortcutOpt') + #13#10;
	end;
	if WantReplace then
		if (not RunReplacer('--replace', code)) or (code <> 0) then
			failures := failures + '- ' + CustomMessage('ReplaceNotepadOpt') +
				' [rc=' + IntToStr(code) + ']'#13#10;
	if WantAssociate then
		if (not RunReplacer('--associate', code)) or (code <> 0) then
			failures := failures + '- ' + CustomMessage('AssociateOpt') +
				' [rc=' + IntToStr(code) + ']'#13#10;
	if WantEditor then
		if (not RunReplacer('--editor', code)) or (code <> 0) then
			failures := failures + '- ' + CustomMessage('SetEditorOpt') +
				' [rc=' + IntToStr(code) + ']'#13#10;
	// 任一失败弹一次汇总,不静默、不判安装失败(ADR 0010 决定 3)。
	if failures <> '' then
		MsgBox(CustomMessage('PostInstallFailed') + #13#10#13#10 + failures,
		       mbError, MB_OK);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
	code: Integer;
	replacer: String;
begin
	// 卸载无条件恢复(ADR 0011):在文件删除之前跑,replacer 可执行文件仍在盘上。
	if CurUninstallStep <> usUninstall then
		Exit;
	replacer := ExpandConstant('{app}\{#MyReplacerExe}');
	if not FileExists(replacer) then
		Exit;
	if (not Exec(replacer, '--restore', ExpandConstant('{app}'),
	             SW_HIDE, ewWaitUntilTerminated, code)) or (code <> 0) then
	// Inno 陷阱:[Code] 内行首的 `[` 会被段解析器误当 section tag,数组构造不得置于行首。
		SuppressibleMsgBox(
			FmtMessage(CustomMessage('UninstallRestoreFailed'), [IntToStr(code)]),
			mbError, MB_OK, IDOK);
end;

[CustomMessages]
english.CreateShortcutOpt=Create a desktop shortcut
chinesesimp.CreateShortcutOpt=创建桌面快捷方式
english.ReplaceNotepadOpt=Replace system notepad.exe with notepad420
chinesesimp.ReplaceNotepadOpt=将系统记事本替换为 notepad420
english.AssociateOpt=Make notepad420 the default opener for text files
chinesesimp.AssociateOpt=关联文本文件打开方式
english.SetEditorOpt=Point terminal Ctrl+G editor (EDITOR/VISUAL) to notepad420
chinesesimp.SetEditorOpt=关联终端 Ctrl+G 外部编辑器(EDITOR/VISUAL)
english.PostInstallFailed=Some post-install actions failed (exit code in brackets). You can fill in the gaps later via notepad420-replacer.exe (GUI or the matching switch):
chinesesimp.PostInstallFailed=部分收尾动作失败(括号内为返回码),可稍后运行 notepad420-replacer.exe(界面勾选或对应开关)补齐:
english.UninstallRestoreFailed=Automatic restore of the original system notepad failed (exit code %1). You can restore it later by running: notepad420-replacer.exe --restore
chinesesimp.UninstallRestoreFailed=自动恢复系统原版记事本失败(返回码 %1)。可稍后手动运行 notepad420-replacer.exe --restore 完成恢复。
