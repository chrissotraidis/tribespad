"""Two pinned font-allocation immediates, independently published by original stores."""
import hashlib,json,pathlib,pefile
from console_fade_inputs import definition
R=pathlib.Path(__file__).resolve().parents[1]
ROWS=[(0x1003d6dc,0x556029,'loc_556021',0x556032),(0x1003d6fd,0x4490ec,'loc_4490E0',0x4490f5)]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def prepare(dll,engine,out,verify_test=True):
    ep=R/'ref/control/Tribes2.exe';qp=R/'ref/qol/IFC22.dll'
    assert sha(ep)=='7ad1d40a6aa74a25d089af6bd6f34fcc20a172fd5932ded82c69fca673e7f8f9'
    assert sha(qp)=='eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607'
    e=pefile.PE(str(ep));q=pefile.PE(str(qp));original=engine.read_text();variants=[];wrappers=['declare i32 @qol_font_size_route_ready(i32)'];records=[]
    for index,(pc,address,name,end) in enumerate(ROWS):
        assert e.get_data(address-1-0x400000,5)==bytes.fromhex('6820020000')
        assert q.get_data(pc-0x10000000,10)==b'\xc7\x05'+address.to_bytes(4,'little')+bytes.fromhex('38020000')
        old=definition(original,name);assert old.count('store i32 544,')==1 and old.count(';    store 0x220, esp, 4')==1
        new=old.replace('store i32 544,','store i32 568,').replace(';    store 0x220, esp, 4',';    store 0x238, esp, 4')
        original=original.replace(old,old.replace('@'+name+'(','@tribes_before_font_'+name+'(',1),1)
        variants.append(new.replace('@'+name+'(','@tribes_font_'+name+'(',1))
        wrappers.append('''declare hidden fastcc void @tribes_font_NAME(%_cpu*)
define hidden fastcc void @NAME(%_cpu* %cpu) {
 %ready = call i32 @qol_font_size_route_ready(i32 INDEX)
 %patched = icmp ne i32 %ready, 0
 br i1 %patched, label %after, label %before
after:
 musttail call fastcc void @tribes_font_NAME(%_cpu* %cpu)
 ret void
before:
 musttail call fastcc void @tribes_before_font_NAME(%_cpu* %cpu)
 ret void
}
'''.replace('NAME',name).replace('INDEX',str(index)))
        writer=definition(dll,'loc_%X'%pc);needle='call void @x86_mov_mem_reg_32(i32 %d, i32 568) nounwind'%address;assert writer.count(needle)==1
        replacement='''%font_offset = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 13
%font_base = load i64, i64* %font_offset, align 8
%font_address = add i64 %font_base, ADDRESS
%font_pointer = inttoptr i64 %font_address to i8*
call void @qol_font_size_write(%_cpu* %cpu, i32 PC, i8* %font_pointer, i32 4, i64 568)'''.replace('ADDRESS',str(address)).replace('PC',str(pc))
        dll=dll.replace(writer,writer.replace(needle,replacement),1)
        records.append(dict(index=index,pc=pc,address=address,name=name,end=end,old_definition_sha256=hashlib.sha256(old.encode()).hexdigest(),new_definition_sha256=hashlib.sha256(new.encode()).hexdigest(),writer_sha256=hashlib.sha256(writer.encode()).hexdigest(),original_bytes=e.get_data(int(name[4:],16)-0x400000,end-int(name[4:],16)).hex()))
    if verify_test:
        test=json.loads((R/'generated/integration-review/font-size/test-result.json').read_text())
        assert test['passed'] and test['cases']==2048 and test['records']==records
        assert test['test_script_sha256']==sha(R/'scripts/test-font-size-patches.py')
        for path,digest in test['inputs_sha256'].items():assert sha(R/path)==digest,path
    source=out/'font-size-engine.ll';source.write_text(original[:original.index('define ')]+'declare hidden fastcc void @loc_401FE0(%_cpu*)\n'+'\n'.join(variants))
    engine.write_text(original+'\n'+'\n'.join(wrappers));dll+='\ndeclare void @qol_font_size_write(%_cpu*, i32, i8*, i32, i64)\n'
    include=out/'font_size_publication.inc';include.write_text('static const TribesCodeWrite font_size_writes[]={'+','.join('{%du,%du,4,544,568}'%(pc,a) for pc,a,_,_ in ROWS)+'};\n')
    report=dict(records=records,engine_source_sha256=sha(engine),variant_sha256=sha(source),include_sha256=sha(include),inputs_sha256={str(p.relative_to(R)):sha(p) for p in [ep,qp,pathlib.Path(__file__).resolve()]},scope='Two independently conditional original font-size writes; each activates only its corresponding precompiled PUSH variant. No runtime native code generation.')
    (out/'font-size-inputs.json').write_text(json.dumps(report,indent=2)+'\n');return dll,source,report
