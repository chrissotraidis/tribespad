#!/usr/bin/env python3
"""Use the pinned HaloPad recursive audit on a Tribes module, in this tree only."""
import argparse, importlib.util, json, pathlib, struct, sys, hashlib
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'ref/halopad/scripts'))
spec=importlib.util.spec_from_file_location('hp_audit',ROOT/'ref/halopad/scripts/audit-executable.py')
hp=importlib.util.module_from_spec(spec);spec.loader.exec_module(hp)
p=argparse.ArgumentParser();p.add_argument('image',type=pathlib.Path);a=p.parse_args()
image=a.image.resolve();out=ROOT/'generated/analysis'/image.stem.lower();out.mkdir(parents=True,exist_ok=True)
import pefile
pe=pefile.PE(str(image));relocs=None if pe.FILE_HEADER.Characteristics&1 else hp.hpmodule.pe_relocations(image)
aud=hp.Audit(image,relocs);aud.run()
if relocs is None: aud.demote_mid_instruction_targets()
else:
 table=dict(relocs)
 for f in list(aud.relocs):
  if f not in table: aud.uncertain[f]=aud.relocs.pop(f)
problems=aud.verify_relocs()
r=aud.report();r.update({'source':str(image.relative_to(ROOT)),'sha256':hashlib.sha256(image.read_bytes()).hexdigest(),'relocation_validation_errors':problems,'limitations':'Inherited Halo compiler heuristics require Tribes-specific oracle validation; decoded coverage is not execution coverage.'})
(out/'audit.json').write_text(json.dumps(r,indent=1)+'\n');(out/'image.bin').write_bytes(aud.img)
(out/'functions.json').write_text(json.dumps({hex(k):v for k,v in sorted(aud.functions.items())},indent=1)+'\n')
(out/'instructions.u32').write_bytes(b''.join(struct.pack('<I',i) for i in sorted(aud.insn)))
(out/'relocations.csv').write_text(''.join(f'{f:#x},{t:#x},\n' for f,(t,k) in sorted(aud.relocs.items())))
srw=out/'srw';srw.mkdir(exist_ok=True)
alltargets={t for _,t in relocs} if relocs is not None else {t for t,k in aud.relocs.values()}
for filename,code in [('fixup_interpret_as_code.sci',True),('fixup_do_not_interpret_as_code.sci',False)]:
 (srw/filename).write_text(''.join(f'loc_{t:X}\n' for t in sorted(alltargets) if aud.in_text(t) and (t in aud.insn)==code))
(srw/'noret_procedures.sci').write_text(''.join(f'loc_{t:X}\n' for t,v in sorted(aud.noret.items()) if v and t in aud.insn))
(srw/'displaced_labels.sci').write_text(''.join(f'loc_{t:X},{d}\n' for t,d in sorted(aud.displaced_tables.items())))
if relocs is not None:
    # Preserve fixed guest addresses where SRW's textual fixup replacement is ambiguous,
    # or a listed target is inside an instruction. Do not rebase this translated image.
    from capstone import x86_const as X
    literal=[]
    for f,t in relocs:
        start=aud.owner.get(f,f)
        ins=aud.decode(start) if aud.in_text(start) else None
        values=[]
        if ins:
            for op in ins.operands:
                if op.type==X.X86_OP_IMM: values.append(op.imm & 0xffffffff)
                if op.type==X.X86_OP_MEM: values.append(op.mem.disp & 0xffffffff)
        if (aud.in_text(t) and t in aud.owner) or values.count(t)>1:
            literal.append(f)
    (srw/'literal_fixups.csv').write_text(''.join(f'{f:#x}\n' for f in sorted(literal)))
    print('fixed-address literal fixups:',len(literal))
print(f'{image.name}: {len(aud.insn)} instructions, {len(aud.functions)} functions, {r["unclassified_bytes"]} unclassified bytes, {len(problems)} relocation errors; {out}')
