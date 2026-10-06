"""Pinned inputs for the first, independently installed console CALL redirect."""
import hashlib, json, pathlib, re

R = pathlib.Path(__file__).resolve().parents[1]
FROZEN = R / 'generated/integration-review/console-patch-checkpoint-20261003'
OUT = R / 'generated/integration-review/console-fade'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()

def load():
    checkpoint = json.loads((FROZEN / 'checkpoint.json').read_text())
    assert checkpoint['passed']
    for row in checkpoint['files']:
        assert sha(FROZEN / row['path']) == row['sha256'], row['path']
    table = json.loads((FROZEN / 'capture/console-patch-pages.json').read_text())
    pages = [(r['address'], (FROZEN / 'capture' / r['file']).read_bytes()) for r in table['ranges']]
    def read(address, size):
        for start, data in pages:
            if start <= address and address + size <= start + len(data):
                return data[address-start:address-start+size]
        raise AssertionError((hex(address), size))
    engine = R / 'generated/integration-review/string-patched-engine/va/engine.ll'
    dll = R / 'generated/srw/run-20261002T220330-ifc22/native-diagnostic/module.ll'
    assert sha(engine) == json.loads(engine.with_name('result.json').read_text())['engine_ll_sha256']
    assert sha(dll) == json.loads(dll.with_name('result.json').read_text())['module_ll_sha256']
    return read, pages, engine, dll

def definition(text, name):
    match = re.search(r'^define (?:hidden|protected) fastcc void @' + name + r'\([^\n]*\n.*?^}', text, re.M | re.S)
    assert match, name
    return match[0] + '\n'
