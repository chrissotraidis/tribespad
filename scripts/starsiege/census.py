#!/usr/bin/env python3
"""Import census for Starsiege: Tribes 1.41 against what TribesPad provides.

Each static import of ref/starsiege/game/Tribes.exe goes in the first class
that matches: A, bound in port/llasm-runtime/win32-bindings.json or named in
config/runtime/*.json; B, named in port/core or port/apple sources; C, imported
by a Tribes 2 module TribesPad already lifts but not provided; D, new. Also
lists the DLLs and functions the game loads by name. Writes
generated/starsiege/census.json and prints a table.

Usage: .venv/bin/python scripts/starsiege/census.py
"""
import collections, json, pathlib, re, sys
import pefile

R = pathlib.Path(__file__).resolve().parents[2]
# Winsock 2 exports TribesPad's 1.41 image imports by ordinal.
WS2_ORDINALS = {1: 'accept', 2: 'bind', 3: 'closesocket', 4: 'connect', 5: 'getpeername', 6: 'getsockname',
                7: 'getsockopt', 8: 'htonl', 9: 'htons', 10: 'ioctlsocket', 11: 'inet_addr', 12: 'inet_ntoa',
                13: 'listen', 14: 'ntohl', 15: 'ntohs', 16: 'recv', 17: 'recvfrom', 18: 'select', 19: 'send',
                20: 'sendto', 21: 'setsockopt', 22: 'shutdown', 23: 'socket', 51: 'gethostbyaddr',
                52: 'gethostbyname', 53: 'getprotobyname', 54: 'getprotobynumber', 55: 'getservbyname',
                56: 'getservbyport', 57: 'gethostname', 101: 'WSAAsyncSelect', 111: 'WSAGetLastError',
                112: 'WSASetLastError', 113: 'WSACancelBlockingCall', 114: 'WSAIsBlocking', 115: 'WSAStartup',
                116: 'WSACleanup', 151: '__WSAFDIsSet'}

def main():
    image = R / 'ref/starsiege/game/Tribes.exe'
    pe = pefile.PE(str(image))
    imports = collections.OrderedDict()
    for d in pe.DIRECTORY_ENTRY_IMPORT:
        dll = d.dll.decode().upper()
        for i in d.imports:
            ordinals = WS2_ORDINALS if dll == 'WS2_32.DLL' else {17: 'InitCommonControls'} if dll == 'COMCTL32.DLL' else {}
            name = i.name.decode() if i.name else ordinals.get(i.ordinal, '%s#%d' % (dll, i.ordinal))
            imports.setdefault(dll, []).append(name)
    bound = set(json.loads((R / 'port/llasm-runtime/win32-bindings.json').read_text()))
    named = set()
    for path in (R / 'config/runtime').glob('*.json'):
        named |= set(re.findall(r'"([A-Za-z_][A-Za-z0-9_]{2,})"', path.read_text()))
    source = set()
    for folder in ['port/core', 'port/apple']:
        for path in (R / folder).rglob('*'):
            if path.suffix in ('.c', '.h', '.m', '.cc', '.cpp', '.inc'):
                source |= set(re.findall(r'[A-Za-z_][A-Za-z0-9_]{2,}', path.read_text(errors='ignore')))
    tribes2 = {f for m in json.loads((R / 'inputs-manifest.json').read_text())['modules']
               for imp in m.get('imports', []) for f in imp['functions']}
    def klass(name):
        if name in bound or name in named:
            return 'A'
        if name in source:
            return 'B'
        if name in tribes2:
            return 'C'
        return 'D'
    table = {dll: {k: sorted(n for n in names if klass(n) == k) for k in 'ABCD'} for dll, names in imports.items()}
    audit = json.loads((R / 'generated/analysis/tribes141/audit.json').read_text())
    dynamic = {'dlls': audit.get('dll_name_strings'),
               'getprocaddress': [{'name': g['name'], 'class': klass(g['name'])} for g in audit.get('getprocaddress_names', [])]}
    out = {'image_sha256': json.loads((R / 'config/starsiege/inputs.json').read_text())['tribes_exe_sha256'],
           'static_imports': sum(len(v) for v in imports.values()), 'by_dll': table, 'loaded_by_name': dynamic,
           'method': __doc__.split('\n\n')[1].replace('\n', ' ')}
    (R / 'generated/starsiege').mkdir(parents=True, exist_ok=True)
    (R / 'generated/starsiege/census.json').write_text(json.dumps(out, indent=1) + '\n')
    print('| DLL | A bound | B source | C T2 import only | D new |')
    print('|---|---:|---:|---:|---:|')
    totals = collections.Counter()
    for dll, row in table.items():
        n = sum(len(v) for v in row.values())
        print('| %s (%d) | %d | %d | %d | %d |' % (dll, n, *(len(row[k]) for k in 'ABCD')))
        totals.update({k: len(row[k]) for k in 'ABCD'})
    print('| **Total (%d)** | **%d** | **%d** | **%d** | **%d** |' % (out['static_imports'], *(totals[k] for k in 'ABCD')))
    print(json.dumps(dynamic))
    for dll, row in table.items():
        if row['D']:
            print(dll, 'D:', ', '.join(row['D']))

if __name__ == '__main__':
    sys.exit(main())

