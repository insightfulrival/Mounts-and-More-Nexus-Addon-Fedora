#!/usr/bin/env python3
"""Small strict PE contract check, without third-party Python dependencies."""
import json
from pathlib import Path
import struct
import sys
import hashlib

def inspect(path):
    data = Path(path).read_bytes()
    u16 = lambda o: struct.unpack_from('<H', data, o)[0]
    u32 = lambda o: struct.unpack_from('<I', data, o)[0]
    assert data[:2] == b'MZ', 'Missing DOS signature'
    pe = u32(0x3c)
    assert data[pe:pe+4] == b'PE\0\0'
    assert u16(pe+4) == 0x8664, 'Expected AMD64'
    assert u16(pe+22) & 0x2000, 'Expected DLL'
    opt = pe+24
    assert u16(opt) == 0x20b, 'Expected PE32+'
    sections = []
    for i in range(u16(pe+6)):
        o = opt+u16(pe+20)+i*40
        sections.append((u32(o+12), max(u32(o+8), u32(o+16)), u32(o+20)))
    def offset(rva):
        for start, size, raw in sections:
            if start <= rva < start+size:
                return raw+rva-start
        raise ValueError(f'Unmapped RVA {rva:x}')
    def string(rva):
        o = offset(rva)
        return data[o:data.index(b'\0', o)].decode('ascii')
    exp = offset(u32(opt+112))
    exports = [string(u32(offset(u32(exp+32))+i*4)) for i in range(u32(exp+24))]
    assert 'GetAddonDef' in exports, exports
    imports = []
    rva = u32(opt+120)
    if rva:
        o = offset(rva)
        while any(data[o:o+20]):
            imports.append(string(u32(o+12)))
            o += 20
    forbidden = [i for i in imports if any(x in i.lower() for x in ('libstdc++', 'libgcc', 'libwinpthread', 'vcruntime', 'msvcp'))]
    assert not forbidden, f'Unexpected compiler runtime DLLs: {forbidden}'
    return {'file':str(path), 'sha256':hashlib.sha256(data).hexdigest(),
            'size':len(data), 'machine':'AMD64', 'format':'PE32+ DLL', 'exports':exports, 'imports':imports}

if __name__ == '__main__':
    result = inspect(sys.argv[1])
    output = Path(__file__).resolve().parents[1]/'build'/'pe-verification.json'
    output.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))
