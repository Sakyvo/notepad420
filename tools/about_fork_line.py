"""Insert the fork attribution line into IDD_ABOUT (en + zh-Hans).

Grows the dialog by 8 dialog units and shifts every control at or below the
first copyright row down by 8. Idempotent via the IDC_FORK_STATIC sentinel.
"""
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

EN_LINE = '    LTEXT           "notepad420 \u2014 personal fork by Sakyvo, based on Notepad4 (zufuliu/notepad4).",IDC_FORK_STATIC,45,38,200,8'
ZH_LINE = '    LTEXT           "notepad420 \u2014 Sakyvo \u7684\u4e2a\u4eba fork,\u57fa\u4e8e Notepad4 (zufuliu/notepad4)\u3002",IDC_FORK_STATIC,45,38,200,8'

FILES = {'src/Notepad4.rc': EN_LINE, 'locale/zh-Hans/Notepad4.rc': ZH_LINE}
SHIFT = 8
CTRL_PREFIXES = ('    LTEXT', '    CONTROL', '    DEFPUSHBUTTON', '    PUSHBUTTON')


def shift_y(line, delta):
    """Shift a dialog control's y. Coordinates are always the last four
    comma-separated fields (x, y, w, h) for every control kind here."""
    parts = line.split(',')
    if len(parts) < 5:
        return line, None
    try:
        x, y, w, h = (int(v) for v in parts[-4:])
    except ValueError:
        return line, None
    parts[-3] = str(y + delta)
    return ','.join(parts), y


def main():
    for path, newline in FILES.items():
        p = Path(path)
        lines = p.read_text(encoding='utf-8').split('\n')
        if any('IDC_FORK_STATIC' in l for l in lines):
            print(f'{path}: already applied')
            continue
        hdr = next(i for i, l in enumerate(lines) if l.startswith('IDD_ABOUT DIALOGEX'))
        end = next(i for i, l in enumerate(lines) if l.strip() == 'END' and i > hdr)
        lines[hdr] = lines[hdr].replace(', 255, 144', ', 255, 152')
        insert_at = None
        for i in range(hdr + 1, end):
            l = lines[i]
            if not l.startswith(CTRL_PREFIXES):
                continue
            shifted, y = shift_y(l, SHIFT)
            if y is None:
                continue
            if y == 38 and l.lstrip().startswith('LTEXT'):
                insert_at = i  # the first copyright row: the new line takes this slot
            if y >= 38:
                lines[i] = shifted
        assert insert_at is not None, 'copyright row not found'
        lines.insert(insert_at, newline)
        p.write_text('\n'.join(lines), encoding='utf-8', newline='')
        print(f'{path}: inserted at line {insert_at + 1}')

    for path in FILES:
        lines = Path(path).read_text(encoding='utf-8').split('\n')
        hdr = next(i for i, l in enumerate(lines) if l.startswith('IDD_ABOUT DIALOGEX'))
        end = next(i for i, l in enumerate(lines) if l.strip() == 'END' and i > hdr)
        print(f'--- {path} ---')
        print('\n'.join(lines[hdr:end + 1]))


main()