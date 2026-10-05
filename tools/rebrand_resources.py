"""Apply the notepad420 rebrand to the two resource scripts (en + zh-Hans).

Product-visible strings only; internal identifiers, file names of the project,
icons and other locale satellites stay on upstream naming (see ADR 0003).
Idempotent: re-running reports 0 replacements.
"""
import io, sys

EN = 'src/Notepad4.rc'
ZH = 'locale/zh-Hans/Notepad4.rc'

PATCHES = {
    EN: [
        ('MENUITEM "&Minimize Notepad4",', 'MENUITEM "&Minimize notepad420",'),
        ('MENUITEM "E&xit Notepad4",', 'MENUITEM "E&xit notepad420",'),
        ('MENUITEM "&Open Notepad4.ini\\tCtrl+F7",', 'MENUITEM "&Open notepad420.ini\\tCtrl+F7",'),
        ('MENUITEM "&About Notepad4\\tF1",', 'MENUITEM "&About notepad420\\tF1",'),
        ('MENUITEM "&Open Notepad4",', 'MENUITEM "&Open notepad420",'),
        ('CAPTION "Notepad4"', 'CAPTION "notepad420"'),
        ('"Add Notepad4 to Windows Explorer\'s context menu."',
         '"Add notepad420 to Windows Explorer\'s context menu."'),
        ('IDS_APPTITLE            "Notepad4"', 'IDS_APPTITLE            "notepad420"'),
        ('IDS_APPTITLE_PASTEBOARD "Notepad4: Paste Board"',
         'IDS_APPTITLE_PASTEBOARD "notepad420: Paste Board"'),
        ('IDS_LINKDESCRIPTION     "Edit with Notepad&4"',
         'IDS_LINKDESCRIPTION     "Edit with notepad420"'),
        ('"Existing Notepad4 window is busy or has an active dialog box.\\nWould you like to open another Notepad4 window?"',
         '"Existing notepad420 window is busy or has an active dialog box.\\nWould you like to open another notepad420 window?"'),
        ('"Changing the UI language requires a restart of Notepad4, restart now?"',
         '"Changing the UI language requires a restart of notepad420, restart now?"'),
    ],
    ZH: [
        ('MENUITEM "最小化 Notepad4(&M)",', 'MENUITEM "最小化 notepad420(&M)",'),
        ('MENUITEM "退出 Notepad4(&X)",', 'MENUITEM "退出 notepad420(&X)",'),
        ('MENUITEM "打开 Notepad4.ini(&O)\\tCtrl+F7",', 'MENUITEM "打开 notepad420.ini(&O)\\tCtrl+F7",'),
        ('MENUITEM "关于 Notepad4(&A)\\tF1",', 'MENUITEM "关于 notepad420(&A)\\tF1",'),
        ('MENUITEM "打开 Notepad4(&O)",', 'MENUITEM "打开 notepad420(&O)",'),
        ('CAPTION "Notepad4"', 'CAPTION "notepad420"'),
        ('"将 Notepad4 添加到 Windows 资源管理器的右键菜单"',
         '"将 notepad420 添加到 Windows 资源管理器的右键菜单"'),
        ('IDS_APPTITLE            "Notepad4"', 'IDS_APPTITLE            "notepad420"'),
        ('IDS_APPTITLE_PASTEBOARD "Notepad4: 粘贴板"',
         'IDS_APPTITLE_PASTEBOARD "notepad420: 粘贴板"'),
        ('IDS_LINKDESCRIPTION     "使用 Notepad4 编辑(&4)"',
         'IDS_LINKDESCRIPTION     "使用 notepad420 编辑"'),
        ('"现有的 Notepad4 窗口正忙或有一个活动的对话框。\\n要打开一个新的窗口吗?"',
         '"现有的 notepad420 窗口正忙或有一个活动的对话框。\\n要打开一个新的窗口吗?"'),
        ('"更改界面语言需要重新启动 Notepad4,现在就重新启动吗?"',
         '"更改界面语言需要重新启动 notepad420,现在就重新启动吗?"'),
    ],
}

total = 0
for path, pairs in PATCHES.items():
    s = io.open(path, encoding='utf-8').read()
    hits = 0
    for old, new in pairs:
        n = s.count(old)
        if n == 0:
            continue
        s = s.replace(old, new)
        hits += n
    if hits:
        io.open(path, 'w', encoding='utf-8', newline='').write(s)
    print(f'{path}: {hits} replacements')
    total += hits
print(f'total: {total}')
sys.exit(0 if total else 0)