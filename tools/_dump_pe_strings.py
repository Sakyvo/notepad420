"""Dump text-bearing resources from the reference replacer tool."""
import sys

import pefile

PATH = r'K:/Notepad3/记事本替换工具_v1.26.exe'
WANT = {4, 5, 6, 10, 23, 24}  # MENU/DIALOG/STRING/RCDATA/RCDATA/MANIFEST

pe = pefile.PE(PATH)
if not hasattr(pe, 'DIRECTORY_ENTRY_RESOURCE'):
    print('no resource directory')
    sys.exit(0)

for res in pe.DIRECTORY_ENTRY_RESOURCE.entries:
    for entry in res.directory.entries:
        for data in entry.directory.entries:
            offset = pe.get_offset_from_rva(data.data.struct.OffsetToData)
            blob = pe.__data__[offset:offset + data.data.struct.Size]
            if res.id not in WANT:
                continue
            print(f'=== RT {res.id} size={data.data.struct.Size} ===')
            parts = blob.decode('utf-16-le', errors='ignore').split(chr(0))
            for part in parts:
                clean = part.strip('\x00').strip()
                if len(clean) >= 3 and any(c.isalpha() or ord(c) > 255 for c in clean):
                    print('  ', repr(clean))
pe.close()