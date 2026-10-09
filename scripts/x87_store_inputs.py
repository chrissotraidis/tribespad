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
    return sources
