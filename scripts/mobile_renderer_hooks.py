"""Select the pinned original 18-hook renderer layout without SDF font hooks.

GL4ES has no verified texture-array font path. Original initialization therefore
omits eight registrations. Retain those original engine entries, compact the two
following renderer trampolines and seven audio trampolines, and keep exact bytes,
write plans, callback checks and precompiled native targets for every active hook.
"""
import copy, hashlib, json, pathlib, re
from console_fade_inputs import definition
R=pathlib.Path(__file__).resolve().parents[1]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
OMITTED={0x449190,0x446070,0x449bb0,0x449c30,0x449cf0,0x449d90,0x4ff8c0,0x4c2010}
PLAN_SHA={
    'renderer-hook-bank':'4cd3d94d28a4b99690be304dde1f6954ea836e21f59e2fc3f72ad08f0f449701',
    'renderer-hook-bank-before-font':'dd74bc60c019057b461da11341b3be225691556ba3bb680be90cc0673812bd2b',
    'audio-hook-bank':'12c9f557a6feeee674b0e6d01e30c0ba6fb1d8ca0ca677ef6e9bec95f84b7674',
}

def relocate(row,address):
    row=copy.deepcopy(row);code=bytearray.fromhex(row['code_hex'])
    assert code[:-5]==bytes.fromhex(row['restore_hex']) and code[-5]==0xe9
    assert (row['address']+len(code)+int.from_bytes(code[-4:],'little',signed=True))&0xffffffff==row['continuation']
    code[-4:]=((row['continuation']-address-len(code))&0xffffffff).to_bytes(4,'little')
    row.update(address=address,code_hex=code.hex());return row

def publication(plan,label):
    rows=sorted(plan['trampolines'],key=lambda r:r['address'])
    lines=['static const TribesCodeWrite '+label+'_hook_writes[]={'+','.join('{%du,%du,%du,UINT64_C(%d),UINT64_C(%d)}'%(w['pc'],w['address'],w['size'],w['before'],w['value']) for w in plan['code_writes'])+'};']
    for i,r in enumerate(rows):lines.append('static const unsigned char %s_bank_%d[]={%s};'%(label,i,','.join(map(str,bytes.fromhex(r['code_hex'])))))
    lines.append('static const TribesPublishedCodeRange '+label+'_bank_ranges[]={'+','.join('{%du,%du,%s_bank_%d}'%(r['address'],len(bytes.fromhex(r['code_hex'])),label,i) for i,r in enumerate(rows))+'};')
    lines.append('static const uint32_t '+label+'_hook_flushes[][2]={'+','.join('{%du,%du}'%(r['original_target'],r['moved_code_bytes']) for r in plan['trampolines'])+',{0x3c0000u,65536u}};')
    lines.append('static const uint32_t '+label+'_hook_callbacks[][2]={'+','.join('{%du,%du}'%(r['callback_slot'],r['address']) for r in plan['trampolines'])+'};')
    return '\n'.join(lines)+'\n'

def prepare(sources,out):
    for name,digest in PLAN_SHA.items():assert sha(R/'generated/integration-review'/name/'plan.json')==digest,name
    renderer=json.loads((R/'generated/integration-review/renderer-hook-bank/plan.json').read_text())
    audio=json.loads((R/'generated/integration-review/audio-hook-bank/plan.json').read_text())
    historical=json.loads((R/'generated/integration-review/renderer-hook-bank-before-font/plan.json').read_text())
    assert renderer['verified'] and audio['verified'] and historical['verified']
    assert len(renderer['trampolines'])==26 and len(audio['trampolines'])==7 and len(historical['trampolines'])==18
    removed=[r for r in renderer['trampolines'] if r['original_target'] in OMITTED];assert len(removed)==8
    ordered=sorted((r for r in renderer['trampolines'] if r not in removed),key=lambda r:r['address'])
    mapping={r['address']:0x3c0ea0+72*i for i,r in enumerate(ordered)}
    rows=[relocate(r,mapping[r['address']]) for r in renderer['trampolines'] if r not in removed]
    old={r['address']:r for r in historical['trampolines']}
    for r in rows:
        for key in ['original_target','continuation','detour_target','moved_code_bytes','code_hex','restore_hex','after_hex','callback_slot']:
            assert r[key]==old[r['address']][key],(hex(r['address']),key)
    writes=[w for w in renderer['code_writes'] if not any(r['original_target']<=w['address']<r['continuation'] for r in removed)]
    assert writes==historical['code_writes']
    renderer=dict(renderer,trampolines=rows,code_writes=writes)
    for r in audio['trampolines']:mapping[r['address']]=r['address']-576
    audio=dict(audio,trampolines=[relocate(r,mapping[r['address']]) for r in audio['trampolines']])
    for label,plan in [('renderer',renderer),('audio',audio)]:
        (out/(label+'_hook_publication.inc')).write_text(publication(plan,label))
    sources=list(sources)
    engine=next(p for p in sources if p.name=='engine-routed.ll');text=engine.read_text()
    for target in sorted(OMITTED):
        name='loc_%X'%target;body=definition(text,name)
        assert '@qol_renderer_hooks_route_ready()' in body and '@tribes_before_renderer_hooks_'+name+'(' in body
        replacement='define hidden fastcc void @%s(%%_cpu* %%cpu) {\nmusttail call fastcc void @tribes_before_renderer_hooks_%s(%%_cpu* %%cpu)\nret void\n}\n\n'%(name,name)
        text=text.replace(body,replacement,1)
    mobile=out/engine.name;mobile.write_text(text);sources[sources.index(engine)]=mobile
    dispatch=next(p for p in sources if p.name=='merged-dispatch.ll');text=dispatch.read_text()
    va=re.search(r'@tribes_dispatch_vas = constant \[(\d+) x i32\] \[([^\n]+)\]',text)
    fn=re.search(r'@tribes_dispatch_fns = constant \[(\d+) x ptr\] \[([^\n]+)\]',text)
    addresses=[int(n) for n in re.findall(r'i32 (\d+)',va[2])];functions=re.findall(r'ptr @([^, ]+)',fn[2]);assert len(addresses)==len(functions)==int(va[1])==int(fn[1])
    inactive={r['address'] for r in removed};assert inactive<=set(addresses) and set(mapping)<=set(addresses)
    entries=sorted((mapping.get(a,a),f) for a,f in zip(addresses,functions) if a not in inactive)
    assert len({a for a,_ in entries})==len(entries)==len(addresses)-8
    text=text.replace(va[0],'@tribes_dispatch_vas = constant [%d x i32] ['%len(entries)+', '.join('i32 %d'%a for a,_ in entries)+']')
    text=text.replace(fn[0],'@tribes_dispatch_fns = constant [%d x ptr] ['%len(entries)+', '.join('ptr @'+f for _,f in entries)+']')
    text=text.replace('@tribes_dispatch_count = constant i32 '+str(len(addresses)),'@tribes_dispatch_count = constant i32 '+str(len(entries)))
    mobile=out/dispatch.name;mobile.write_text(text);sources[sources.index(dispatch)]=mobile
    report=dict(renderer_hooks=18,renderer_writes=len(writes),audio_hooks=7,omitted_targets=sorted(OMITTED),dispatch_relocations=mapping,renderer=renderer,audio=audio,scope='Finite original legacy-font layout. Native trampoline bodies retain their original instruction semantics and continuation; all moved x86 branch displacements are recomputed and validated at runtime.')
    (out/'mobile-hooks.json').write_text(json.dumps(report,indent=2)+'\n')
    return sources,report
