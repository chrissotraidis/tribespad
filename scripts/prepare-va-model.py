#!/usr/bin/env python3
"""Prepare build25034's original guest-address model; no game-service stubs.

Finite ARM64 dispatch replaces indirect x86 transfers. Data is loaded at original
PE VAs in a separate reserved guest memory window. Derived from the pinned
HaloPad address-model design, with Tribes import spellings and strict input gates.
"""
import argparse, hashlib, json, pathlib, re, subprocess
import pefile
ROOT = pathlib.Path(__file__).resolve().parents[1]
IMPORT_BASE, IMPORT_STRIDE, HOST_RETURN = 0xfffe0000, 16, 0xfffff000
REGS = {'eax','ebx','ecx','edx','esi','edi','ebp','esp','tmpadr','tmpcnd'}
LABEL = r'(?<![\w.])(loc_([0-9A-F]+))'
PLAIN = re.compile(LABEL)
PLUS = re.compile(r'\(\s*'+LABEL+r'\s*\+\s*\((-?\d+)\)\s*\)')
INDEX = re.compile(LABEL+r'\[(-?\d+)\]')

def value_labels(line):
    line = PLUS.sub(lambda m: hex((int(m[2],16)+int(m[3])) & 0xffffffff),line)
    line = INDEX.sub(lambda m: hex((int(m[2],16)+int(m[3])) & 0xffffffff),line)
    return PLAIN.sub(lambda m:hex(int(m[2],16)),line)

def symbol(name):
    return 'tbimp_' + re.sub(r'[^A-Za-z0-9_]',lambda m:'_%02x'%ord(m[0]),name)

def replace_names(line, mapping):
    # Longest match and exact lexical boundaries preserve MSVC decorations.
    if not mapping:return line
    pattern = r'(?<![\w$@?.])('+'|'.join(re.escape(n) for n in sorted(mapping,key=len,reverse=True))+r')(?![\w$@?.])'
    return re.sub(pattern,lambda m:mapping[m[0]],line)

def transform(lines, imports=None):
    imports = imports or {}
    import_values={k:hex(v) for k,v in imports.items()}
    import_pattern=re.compile(r'(?<![\w$@?.])('+'|'.join(re.escape(n) for n in sorted(imports,key=len,reverse=True))+r')(?![\w$@?.])') if imports else None
    out=[]; stats={'indirect_transfers':0,'value_labels':0,'import_values':0}
    for raw in lines:
        line=raw.rstrip('\n'); words=line.strip().split()
        if not words or words[0].startswith(';'):out.append(line);continue
        op=words[0]
        if re.search(r'\btmp19\b',line):raise ValueError('Reserved address temporary tmp19 is already used')
        is_reg=lambda n:n in REGS or re.fullmatch(r'tmp\d+',n) is not None
        if op=='tcall' and len(words)==2 and is_reg(words[1]):
            out.extend(['call tribes_set_dispatch_target $, '+words[1],'tcall tribes_dispatch']);stats['indirect_transfers']+=1;continue
        if op in ('proc','endp','tcall','ctcallz','ctcallnz'):
            if op.startswith('ctcall') and is_reg(words[-1]):raise ValueError('Conditional indirect transfer: '+line)
            out.append(line);continue
        replaced=import_pattern.sub(lambda m:import_values[m[0]],line) if import_pattern else line
        if replaced!=line:stats['import_values']+=1;line=replaced
        if op.startswith(('load','store')):
            parts=[x.strip() for x in line.strip()[len(op):].split(',')]
            if len(parts)>=2 and (PLAIN.search(parts[1]) or re.fullmatch(r'0x[0-9a-fA-F]+',parts[1])):
                out.append('mov tmp19, '+value_labels(parts[1]));parts[1]='tmp19'
                out.append(op+' '+', '.join(value_labels(x) for x in parts));stats['value_labels']+=1;continue
        new=value_labels(line);stats['value_labels']+=new!=line;out.append(new)
    return out,stats

def postprocess(raw, output):
    procs=[];removed=0;skip=False
    with raw.open() as src,output.open('w') as dst:
        dst.write('target triple = "arm64-apple-macos12"\n')
        for line in src:
            if skip:skip=line.strip()!='}';continue
            if line.startswith('define protected ccc void @c_'):skip=True;removed+=1;continue
            line=line.replace('define private fastcc','define hidden fastcc',1)
            if line.startswith('define hidden fastcc void @__return_procedure('):
                line=line.replace('define hidden','define internal',1)
            if 'ptrtoint' in line and re.search(r'ptrtoint void\s*\(%_cpu\*\)\*',line):
                raise ValueError('Host function pointer becomes a guest value: '+line.strip())
            m=re.match(r'define (?:hidden|protected) fastcc void @(loc_[0-9A-F]+)\(',line)
            if m:procs.append(m[1])
            dst.write(line)
    return procs,removed

def dispatch_ir(entries):
    n=len(entries)
    lines=['target triple = "arm64-apple-macos12"']
    lines.extend('declare hidden fastcc void @%s(ptr)'%name for name in sorted({name for _,name in entries}))
    lines.extend(['@tribes_dispatch_count = constant i32 '+str(n),
      '@tribes_dispatch_vas = constant [%d x i32] ['%n+', '.join('i32 '+str(v) for v,_ in entries)+']',
      '@tribes_dispatch_fns = constant [%d x ptr] ['%n+', '.join('ptr @'+s for _,s in entries)+']'])
    # Pointer prefix offsets are independently asserted by the native runtime.
    lines.append('''
declare ptr @tribes_lookup(i32)
define hidden fastcc void @tribes_dispatch(ptr %cpu) nounwind {
  %mailbox = getelementptr i8, ptr %cpu, i64 172
  %target = load i32, ptr %mailbox
  %fn = call ptr @tribes_lookup(i32 %target)
  musttail call fastcc void %fn(ptr %cpu)
  ret void
}
define hidden fastcc void @tribes_return_to_host(ptr %cpu) nounwind { ret void }
define void @tribes_enter(ptr %cpu, i32 %va) nounwind {
  %sp = getelementptr i8, ptr %cpu, i64 16
  %esp = load i32, ptr %sp
  %next = sub i32 %esp, 4
  store i32 %next, ptr %sp
  %basep = getelementptr i8, ptr %cpu, i64 112
  %base = load i64, ptr %basep
  %offset = zext i32 %next to i64
  %addr = add i64 %base, %offset
  %p = inttoptr i64 %addr to ptr
  store i32 4294963200, ptr %p, align 1
  %fn = call ptr @tribes_lookup(i32 %va)
  call fastcc void %fn(ptr %cpu)
  ret void
}
''')
    return '\n'.join(lines)+'\n'

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('lift',type=pathlib.Path);a=ap.parse_args()
    work=a.lift.resolve();result=json.loads((work/'result.json').read_text())
    assert result['exit']==0 and result['full_flags'] and not result['diagnostic']
    exe=work/'Tribes2.exe';assert hashlib.sha256(exe.read_bytes()).hexdigest()==result['image_sha256']
    tool=ROOT/'generated/tools/llasm/llasm';assert hashlib.sha256(tool.read_bytes()).hexdigest()==json.loads((ROOT/'toolchains.lock.json').read_text())['artifacts']['llasm/llasm']
    out=work/'va';out.mkdir(exist_ok=True)
    active_report=out/'compile.json'
    if active_report.exists():
        active=json.loads(active_report.read_text())
        if active.get('state')=='running':
            probe=subprocess.run(['ps','-p',str(active['pid']),'-o','command='],capture_output=True,text=True)
            if probe.returncode==0 and str(out/'engine.ll') in probe.stdout:raise SystemExit('Cannot rewrite input of live compiler: '+str(active['pid']))
    (out/'result.json').write_text(json.dumps({'prepared':False,'playable':False,'whole_game_entered':False})+'\n')
    main=(work/'tribes2.llasm').read_text().splitlines();extern=(work/'extern.llinc').read_text()
    known=set(re.findall(r'^proc (\S+) external','\n'.join(main)+'\n'+extern,re.M))
    pe=pefile.PE(str(exe));imports=[]
    for dll in pe.DIRECTORY_ENTRY_IMPORT:
        for imp in dll.imports:
            name=imp.name.decode() if imp.name else dll.dll.decode().split('.')[0]+'_ord'+str(imp.ordinal)
            assert name in known,(dll.dll,name)
            candidates=['ws2_32.'+name,dll.dll.decode().split('.')[0]+'_ord'+str(imp.ordinal)] if imp.import_by_ordinal else []
            sr_name=next((candidate for candidate in candidates if candidate in known),name)
            imports.append({'slot':imp.address,'name':name,'dll':dll.dll.decode().lower(),'symbol':symbol(name),'sr_name':sr_name,'ordinal':imp.ordinal})
    imports.sort(key=lambda x:x['slot']);assert IMPORT_BASE+len(imports)*IMPORT_STRIDE < HOST_RETURN
    values={}
    for i,row in enumerate(imports):row['guest_va']=IMPORT_BASE+i*IMPORT_STRIDE;values[row['name']]=row['guest_va'];values[row['sr_name']]=row['guest_va']
    aliases=sorted({'define '+name+' '+row['symbol'] for row in imports for name in (row['name'],row['sr_name'])})
    import_names=set(values)
    extern='\n'.join(line for line in extern.splitlines() if not (re.fullmatch(r'proc (\S+) external',line) and line.split()[1] in import_names))
    declarations=['proc '+name+' external' for name in sorted({row['symbol'] for row in imports})]
    (out/'extern.llinc').write_text('\n'.join(aliases+declarations)+'\n'+extern+'\nproc tribes_dispatch external\nfuncv tribes_set_dispatch_target $, target\n')
    (out/'macros.llinc').write_text('')
    code,stats=transform((work/'seg01_code.llinc').open(),values)
    (out/'seg01_code.va.llinc').write_text('\n'.join(code)+'\n')
    kept=[];skip=False
    for line in main:
        if re.fullmatch(r'proc (\S+) external',line) and line.split()[1] in import_names:continue
        if line.startswith('datasegment '):skip=True;continue
        if skip:
            if line.startswith('endd'):skip=False
            continue
        kept.append('include seg01_code.va.llinc' if line=='include seg01_code.llinc' else line)
    (out/'tribes2.va.llasm').write_text('\n'.join(kept)+'\n')
    cmd=[str(tool),'-m64','-ptrofs','-O2','-I',str(out),'-I',str(ROOT/'port/llasm-support'),'-I',str(ROOT/'ref/halopad/port/llasm-support'),'-o',str(out/'raw.ll'),str(out/'tribes2.va.llasm')]
    with (out/'llasm.log').open('w') as log:subprocess.run(cmd,cwd=out,stdout=log,stderr=log,check=True)
    procs,removed=postprocess(out/'raw.ll',out/'engine.ll')
    table=[(int(n[4:],16),n) for n in procs]+[(x['guest_va'],x['symbol']) for x in imports]
    assert len({v for v,_ in table})==len(table)
    (out/'dispatch.ll').write_text(dispatch_ir(sorted(table)))
    report={'cpu_header_sha256':hashlib.sha256((ROOT/'port/llasm-support/llasm_cpu.h').read_bytes()).hexdigest(),'dispatch_model':'per-CPU mailbox at byte 172','prepared':True,'engine_ll_sha256':hashlib.sha256((out/'engine.ll').read_bytes()).hexdigest(),'playable':False,'image_sha256':result['image_sha256'],'imports':imports,'stats':stats,'procedures':len(procs),'wrappers_removed':removed,'dispatch_entries':len(table),'host_return':HOST_RETURN,'whole_game_entered':False,'support_sha256':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for d in [ROOT/'port/llasm-support',ROOT/'ref/halopad/port/llasm-support'] for p in d.glob('*.llinc')}}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k not in ('imports','support_sha256')},indent=2))
if __name__=='__main__':main()
