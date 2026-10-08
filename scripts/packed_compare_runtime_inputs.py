"""Replace four reached instruction traps with byte-verified native semantics."""
import hashlib,json,pathlib
import pefile
R=pathlib.Path(__file__).resolve().parents[1]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
ROWS=[(0x1002f584,'0fc2c801','cmpltps',1,0),(0x1002f58c,'0f5dc2','minps',0,2),
      (0x1002f5dd,'0fc2d001','cmpltps',2,0),(0x1002f5e8,'0f5dc1','minps',0,1)]
def translate(text):
    image=R/'ref/qol/IFC22.dll'
    assert sha(image)=='eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607'
    pe=pefile.PE(str(image))
    for pc,raw,op,dest,source in ROWS:
        assert pe.get_data(pc-0x10000000,len(bytes.fromhex(raw)))==bytes.fromhex(raw)
        needle=f'call void @halopad_trap_unimplemented(i32 {pc}) nounwind'
        assert text.count(needle)==1,(hex(pc),'changed/missing original instruction')
        text=text.replace(needle,f'call void @tb_sse_packed_{op}(%_cpu* %cpu, i32 {dest}, i32 {source}) nounwind')
    for op in ['cmpltps','minps']:
        assert f'declare void @tb_sse_packed_{op}' not in text
        text+=f'\ndeclare void @tb_sse_packed_{op}(%_cpu*, i32, i32)\n'
    return text
def prepare(text):
    proof=json.loads((R/'generated/integration-review/packed-compare/result.json').read_text())
    assert proof['passed']
    for name,digest in proof['sources_sha256'].items():assert sha(R/name)==digest,name
    return translate(text),R/'port/llasm-support/tribes_packed_compare.c',dict(sites=[hex(r[0]) for r in ROWS],test_sha256=sha(R/'generated/integration-review/packed-compare/result.json'))
