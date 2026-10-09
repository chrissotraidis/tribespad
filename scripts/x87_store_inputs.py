"""Use freshly relifted store definitions while retaining validated patch routing."""
import hashlib
import json
import pathlib
import re

R = pathlib.Path(__file__).resolve().parents[1]
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
DEFINITION = re.compile(r'^define [^\n]*@([A-Za-z_0-9]+)\([^\n]*\n.*?^}\n', re.M | re.S)

def semantic(body):
    return '\n'.join(line for line in body.splitlines() if line and not line.startswith(';'))

def changes():
    old = R / 'generated/integration-review/patched-engine/va/engine.ll'
    new = R / 'generated/integration-review/x87-store-engine/va/engine.ll'
    for p in [old, new]:
        assert sha(p) == json.loads(p.with_name('result.json').read_text())['engine_ll_sha256']
    test_path = R / 'generated/integration-review/x87-indexed-stores/result.json'
    test = json.loads(test_path.read_text())
    assert test['passed']
    for name, digest in test['source_sha256'].items():
        assert sha(R / name) == digest, name
    lift = new.parent.parent
    meta = json.loads((lift / 'result.json').read_text())
    assert meta['srw_sha256'] == test['srw_sha256']
    old_code = (old.parent.parent / 'seg01_code.llinc').read_text()
    new_code = (lift / 'seg01_code.llinc').read_text()
    pattern = re.compile(r'^(FSTP?_FLOAT)\nmov tmp1, tmp0\n(.*?)^store tmp1, ([^\n]+)$', re.M | re.S)
    def revert(m):
        assert '\n;' not in '\n' + m[2] and 'FST' not in m[2] and 'endp' not in m[2]
        return m[1] + '\n' + m[2] + 'store tmp0, ' + m[3]
    reverted, count = pattern.subn(revert, new_code)
    assert count == 13852 and reverted == old_code
    previous = {m[1]: m[0] for m in DEFINITION.finditer(old.read_text())}
    current = {m[1]: m[0] for m in DEFINITION.finditer(new.read_text())}
    assert previous.keys() == current.keys()
    selected = {name: (previous[name], body) for name, body in current.items()
                if semantic(previous[name]) != semantic(body)}
    assert selected and all('@x87_fst' in before for before, _ in selected.values())
    return selected, dict(old_sha256=sha(old), new_sha256=sha(new),
                         regression_sha256=sha(test_path), preserved_stores=count)

def prepare(sources, out):
    selected, report = changes()
    source = next(p for p in sources if p.name == 'engine-routed.ll')
    found = {}
    def replace(m):
        name = m[1]
        suffix = re.search(r'loc_[0-9A-F]+$', name)
        if not suffix or suffix[0] not in selected:
            return m[0]
        original = suffix[0]
        before, after = selected[original]
        # Routing wrappers remain untouched. Only the exact old original body,
        # possibly renamed by a verified publication step, can be replaced.
        expected = before.replace('@' + original + '(', '@' + name + '(', 1)
        if semantic(m[0]) != semantic(expected):
            return m[0]
        found.setdefault(original, []).append(name)
        return after.replace('@' + original + '(', '@' + name + '(', 1)
    text = DEFINITION.sub(replace, source.read_text())
    assert found.keys() == selected.keys(), ('Unmatched original definitions', sorted(selected.keys() - found.keys()))
    # This is a derived compiler input, not a mutation of the frozen receipt.
    dest = out / 'x87-engine-routed.ll'
    dest.write_text(text)
    sources = list(sources)
    sources[sources.index(source)] = dest
    report.update(source_sha256=sha(source), output_sha256=sha(dest), functions=found,
                  script_sha256=sha(pathlib.Path(__file__).resolve()),
                  scope='Fresh strict compiler definitions substituted before checked-memory instrumentation. Startup routing and publication plans unchanged.')
    (out / 'x87-stores.json').write_text(json.dumps(report, indent=2) + '\n')
    sources = prepare_mp3(sources, out)
    return sources

def mp3_changes():
    old = R / 'generated/srw/run-20261004T210704-mp3dec/native-diagnostic/module.ll'
    new = R / 'generated/srw/run-20261009T191518-mp3dec/native-diagnostic/module.ll'
    reports = [json.loads(p.with_name('result.json').read_text()) for p in [old, new]]
    for p, report in zip([old, new], reports):
        assert sha(p) == report['module_sha256']
    for key in ['image_sha256', 'mapped_sha256', 'entries', 'imports', 'exports', 'unsupported_instructions']:
        assert reports[0][key] == reports[1][key], key
    test = json.loads((R / 'generated/integration-review/x87-indexed-stores/result.json').read_text())
    assert test['passed']
    for name, digest in test['source_sha256'].items():
        assert sha(R / name) == digest, name
    identity = json.loads((new.parent.parent / 'result.json').read_text())
    assert identity['srw_sha256'] == test['srw_sha256']
    pattern = re.compile(r'^(FSTP?_FLOAT)\nmov tmp1, tmp0\n(.*?)^store tmp1, ([^\n]+)$', re.M | re.S)
    def revert(m):
        assert '\n;' not in '\n' + m[2] and 'FST' not in m[2] and 'endp' not in m[2]
        return m[1] + '\n' + m[2] + 'store tmp0, ' + m[3]
    reverted, count = pattern.subn(revert, (new.parent.parent / 'seg01_code.llinc').read_text())
    assert count == 560 and reverted == (old.parent.parent / 'seg01_code.llinc').read_text()
    previous = {m[1]: m[0] for m in DEFINITION.finditer(old.read_text())}
    current = {m[1]: m[0] for m in DEFINITION.finditer(new.read_text())}
    assert previous.keys() == current.keys()
    selected = {n: (previous[n], body) for n, body in current.items() if semantic(previous[n]) != semantic(body)}
    assert len(selected) == 19
    # Apart from comments, only stored SSA values may change. Validate their
    # provenance from FST/FSTP and keep destinations and control flow identical.
    store = re.compile(r'^store i32 (%\d+), (i32\* %\d+, align \d+)$')
    for name, (before, after) in selected.items():
        a, b = semantic(before).splitlines(), semantic(after).splitlines()
        assert len(a) == len(b), name
        for left, right in zip(a, b):
            if left == right:
                continue
            x, y = store.fullmatch(left), store.fullmatch(right)
            assert x and y and x[2] == y[2], (name, left, right)
            assert re.search(r'^' + re.escape(y[1]) + r' = call i32 @x87_fstp?_float\(', after, re.M), name
    return old, new, selected

def prepare_mp3(sources, out):
    old, new, selected = mp3_changes()
    assert sources.count(old) == 1
    proof_path = R / 'generated/integration-review/mp3-window-tables/result.json'
    proof = json.loads(proof_path.read_text())
    assert proof['passed'] and proof['module_sha256'] == sha(new)
    for name, digest in proof['source_sha256'].items():
        assert sha(R / name) == digest, name
    sources = list(sources)
    sources[sources.index(old)] = new
    (out / 'mp3-stores.json').write_text(json.dumps(dict(
        old_sha256=sha(old), new_sha256=sha(new), functions=sorted(selected),
        regression_sha256=sha(proof_path), script_sha256=sha(pathlib.Path(__file__).resolve()),
        scope='Fresh codec lift before checked-memory instrumentation; all 739 diagnostic stops and original exports retained.'), indent=2) + '\n')
    return sources
