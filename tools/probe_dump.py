# Dump current Windows clipboard: all registered format names + text/plain bytes.
import ctypes, json, sys
from ctypes import wintypes as w

u32 = ctypes.windll.user32
k32 = ctypes.windll.kernel32
u32.OpenClipboard.argtypes = (w.HWND,)
u32.CloseClipboard.argtypes = ()
u32.EnumClipboardFormats.argtypes = (w.UINT,)
u32.EnumClipboardFormats.restype = w.UINT
u32.GetClipboardData.argtypes = (w.UINT,)
u32.GetClipboardData.restype = w.HANDLE
u32.IsClipboardFormatAvailable.argtypes = (w.UINT,)
u32.GetClipboardFormatNameW.argtypes = (w.UINT, w.LPWSTR, ctypes.c_int)
u32.RegisterClipboardFormatW.argtypes = (w.LPCWSTR,)
u32.RegisterClipboardFormatW.restype = w.UINT
k32.GlobalLock.argtypes = (w.HANDLE,); k32.GlobalLock.restype = ctypes.c_void_p
k32.GlobalUnlock.argtypes = (w.HANDLE,)
k32.GlobalSize.argtypes = (w.HANDLE,); k32.GlobalSize.restype = ctypes.c_size_t

def read_bytes(fmt):
    if not u32.IsClipboardFormatAvailable(fmt):
        return None
    h = u32.GetClipboardData(fmt)
    if not h:
        return None
    size = k32.GlobalSize(h)
    p = k32.GlobalLock(h)
    data = ctypes.string_at(p, size) if p else None
    k32.GlobalUnlock(h)
    return data

if not u32.OpenClipboard(None):
    print('cannot open clipboard'); sys.exit(1)
fmt = 0
names = {}
while True:
    fmt = u32.EnumClipboardFormats(fmt)
    if not fmt or fmt in names:
        break
    buf = ctypes.create_unicode_buffer(260)
    n = u32.GetClipboardFormatNameW(fmt, buf, 260)
    names[fmt] = buf.value if n else f'<std:{fmt}>'
print('FORMATS:')
for f, nm in names.items():
    print(f'  0x{f:04x} {nm}')

unip = u32.RegisterClipboardFormatW('Web Custom Format Map')
print('\nmap fmt id: 0x%04x avail=%s' % (unip, bool(u32.IsClipboardFormatAvailable(unip))))
if u32.IsClipboardFormatAvailable(unip):
    m = read_bytes(unip).rstrip(b'\x00')
    print('MAP BYTES len=%d:' % len(m))
    try:
        print(m.decode('utf-8'))
    except Exception as e:
        print(repr(m[:400]))

txt = u32.GetClipboardData(13)  # CF_UNICODETEXT
if txt:
    size = k32.GlobalSize(txt); p = k32.GlobalLock(txt)
    wtxt = ctypes.wstring_at(p, size // 2)
    k32.GlobalUnlock(txt)
    print('\nTEXT repr:', repr(wtxt))
    print('TEXT has LF-only? %s; has CRLF? %s' % ('\r' not in wtxt and '\n' in wtxt, '\r\n' in wtxt))

# the expected payload slot names
for i in range(5):
    nm = 'Web Custom Format%d' % i
    fid = u32.RegisterClipboardFormatW(nm)
    if u32.IsClipboardFormatAvailable(fid):
        b = read_bytes(fid).rstrip(b'\x00')
        print('\nSLOT %s (%d bytes): %s' % (nm, len(b), b[:220].decode('utf-8', 'replace')))
u32.CloseClipboard()
