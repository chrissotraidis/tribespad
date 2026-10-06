#!/usr/bin/env python3
"""Prepare finite image-pinned native compression routes."""
import argparse, hashlib, importlib.util, json, pathlib, struct
import capstone
import pefile
R = pathlib.Path(__file__).resolve().parents[1]
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()

def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
    return m

def prepare(out):
    out = pathlib.Path(out); out.mkdir(parents=True, exist_ok=True)
    route_path = R / 'port/llasm-runtime/qol-zlib-routes.json'
    binding_path = R / 'port/llasm-runtime/qol-zlib-bindings.json'
    routes = json.loads(route_path.read_text()); bindings = json.loads(binding_path.read_text())
    image = R / 'ref/product/IFC22.dll'
    assert sha(image) == routes['image_sha256']
    pe = pefile.PE(str(image)); assert pe.OPTIONAL_HEADER.ImageBase == routes['image_base']
    reference_path = R / 'generated/qol-feature-dispatch/result.json'
    reference = json.loads(reference_path.read_text())
    assert reference['pass'] and reference['image_sha256'] == sha(image)
    assert reference['script_sha256'] == sha(R / 'scripts/test-qol-feature-dispatch.py')
    for name, h in reference['native_source_sha256'].items(): assert sha(R / name) == h
    native = next(p for p in reference['profiles'] if p['name'] == 'native')
    assert native['features'] == [0] * 15
    assert native['runs'][0]['slots'][1:3] == [0x100c1c20, 0x100c1bf0]
    entries = []; replacements = []; replacement_audit = []; constant_audit = []
    for row in routes['routes']:
        va = row['guest_va']; raw = bytes.fromhex(row['entry_bytes_hex'])
        assert pe.get_data(va - routes['image_base'], len(raw)) == raw
        assert bindings[row['binding']]['calling_convention'] == 'cdecl'
        for constant in row.get('constant_ranges', []):
            address = constant['guest_va']; size = constant['length']
            assert size > 0
            data = pe.get_data(address-routes['image_base'], size)
            assert len(data) == size and hashlib.sha256(data).hexdigest() == constant['sha256']
            assert data.hex() == constant['bytes_hex']
            section = pe.get_section_by_rva(address-routes['image_base'])
            assert section is not None and not section.Characteristics & 0x80000000
            assert address-routes['image_base']+size <= section.VirtualAddress+section.Misc_VirtualSize
            constant_audit.append(dict(binding=row['binding'], guest_va=address,
                                       length=size, sha256=constant['sha256'], read_only=True))
        entries.append((va, 'tbimp_' + row['binding']))
        if 'replacement_length' in row:
            length = row['replacement_length']
            body = pe.get_data(va - routes['image_base'], length)
            assert hashlib.sha256(body).hexdigest() == row['replacement_sha256']
            # Refuse an observed external transfer/relocated pointer into the
            # middle of a replacement. This bounded census is not a universal
            # proof that arbitrary computed addresses cannot target the body.
            starts = R / 'generated/analysis/ifc22/instructions.u32'
            decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32); decoder.detail = True
            incoming = []
            for (pc,) in struct.iter_unpack('<I', starts.read_bytes()):
                if va <= pc < va + length: continue
                i = next(decoder.disasm(pe.get_data(pc-routes['image_base'], 15), pc, count=1), None)
                if i and (i.group(capstone.CS_GRP_JUMP) or i.group(capstone.CS_GRP_CALL)):
                    for op in i.operands:
                        if op.type == capstone.x86_const.X86_OP_IMM and va <= op.imm < va + length:
                            assert op.imm == va, ('transfer into replaced body', hex(pc), i.op_str)
                            incoming.append({'pc':hex(pc),'target':hex(op.imm),'assembly':i.mnemonic+' '+i.op_str})
            pointers = []
            for block in getattr(pe, 'DIRECTORY_ENTRY_BASERELOC', []):
                for relocation in block.entries:
                    if relocation.type != 3: continue
                    target = struct.unpack('<I', pe.get_data(relocation.rva, 4))[0]
                    if va <= target < va + length:
                        assert target == va, ('relocated pointer into replaced body', hex(relocation.rva), hex(target))
                        pointers.append({'source':hex(routes['image_base']+relocation.rva),'target':hex(target)})
            replacements.append('loc_%X,%d,tcall tbimp_%s' % (va, length, row['binding']))
            replacement_audit.append({'guest_va':va,'length':length,'incoming_transfers':incoming,
                                      'relocated_pointers':pointers,'instruction_starts_sha256':sha(starts)})
    assert len(entries) == len({va for va, _ in entries}) == 12
    generator = module('bindings', R / 'scripts/gen-win32-bindings.py')
    wrappers = out / 'bindings.ll'
    wrappers.write_text(generator.generate(bindings, include_unsupported_dinput=False))
    va_model = module('va_model', R / 'scripts/prepare-va-model.py')
    # A finite native dispatch model exercises these exact original PCs.
    # Whole-image preparation must merge entries and replace direct transfers;
    # this isolated model does not activate routes in the full client.
    dispatch = out / 'dispatch.ll'
    dispatch.write_text(va_model.dispatch_ir(sorted(entries)))
    replacement_path = out / 'instruction_replacements.sci'
    replacement_path.write_text('\n'.join(replacements) + '\n')
    evidence = [route_path, binding_path, image, reference_path,
                R / 'scripts/gen-win32-bindings.py', R / 'scripts/prepare-va-model.py',
                pathlib.Path(__file__).resolve()]
    report = {'pass': True, 'entries': entries,
              'source_sha256': {str(p.relative_to(R)): sha(p) for p in evidence},
              'wrappers_sha256': sha(wrappers), 'dispatch_sha256': sha(dispatch),
              'replacement_sha256':sha(replacement_path),'replacement_audit':replacement_audit,
              'constant_audit':constant_audit,
              'scope': routes['scope']}
    (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    return report

if __name__ == '__main__':
    ap = argparse.ArgumentParser(); ap.add_argument('--output', type=pathlib.Path,
        default=R / 'generated/zlib-ng/routes'); args = ap.parse_args()
    prepare(args.output); print('PASS: image-pinned scalar cdecl routes prepared')
