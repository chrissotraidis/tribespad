"""Resolve five implemented sync calls when the original ARB_sync group is skipped."""
import hashlib,json,pathlib
from console_fade_inputs import definition
R=pathlib.Path(__file__).resolve().parents[1]
# Original name strings and destination slots; never set capability flags.
SLOTS=[(0x1046c7f8,0x101bd870),(0x1046c840,0x101bd87c),
       (0x1046c820,0x101bd888),(0x1046c830,0x101bd898),(0x1046c81c,0x101bd8c8)]
BODY_SHA={'loc_1001BEB1':'e6ba6c9f1cd624ab9891bd43d04de81e1b0b3201a6b0326fb2a5b7c4cc6c1500',
'loc_1001BEBA':'b83d999a53594a4f1dc7aeaefba1cac01ddae116a6317cc5e74090a94c8dab45',
'loc_1001BEC1':'a50422e5caea3eee91f9b11829f5d70293f4789f29e6a93a1c95d3d8981b238a',
'loc_1001BECD':'655cb5bf375381e16fde5c0adf3cb2663be6cddbd321285025d714e997a420e6',
'loc_1001BED9':'1705abc3c67cb34d80ccb633ef82e45bd6ac11fbd5339cf172a230af1f299715',
'loc_1001BEE5':'f344cc1e0c2120e774c6d445bcace436eb696f2874dc0a92b1250ee009961b5f',
'loc_1001BEFF':'e25a90183534e92b13e1b6a9668855ff2737b6eabbf3a27e910a722d1c2b828b'}

def translate(text):
 assert hashlib.sha256((R/'ref/qol/IFC22.dll').read_bytes()).hexdigest()=='eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607'
 for name,digest in BODY_SHA.items():assert hashlib.sha256(definition(text,name).encode()).hexdigest()==digest,name
 body=definition(text,'loc_1001BEB1');old='l_1:\nmusttail call fastcc void @loc_1001BF07(%_cpu* %cpu) nounwind\nret void';assert body.count(old)==1
 lines=['l_1:']
 for i,(slot,name) in enumerate(SLOTS):
  lines.append(f'''%sync_address_{i} = add i64 %2, {slot}
%sync_pointer_{i} = inttoptr i64 %sync_address_{i} to i32*
%sync_slot_{i} = load i32, i32* %sync_pointer_{i}, align 4
%sync_missing_{i} = icmp eq i32 %sync_slot_{i}, 0
br i1 %sync_missing_{i}, label %sync_resolve_{i}, label %sync_continue_{i}
sync_resolve_{i}:
%sync_provider_{i} = call i32 @tribes_graphics_get_proc(%_cpu* %cpu, i32 {name})
%sync_available_{i} = icmp ne i32 %sync_provider_{i}, 0
br i1 %sync_available_{i}, label %sync_store_{i}, label %sync_continue_{i}
sync_store_{i}:
store i32 %sync_provider_{i}, i32* %sync_pointer_{i}, align 4
br label %sync_continue_{i}
sync_continue_{i}:''')
 lines.append('musttail call fastcc void @loc_1001BF07(%_cpu* %cpu) nounwind\nret void')
 result=text.replace(body,body.replace(old,'\n'.join(lines)),1)
 if '@tribes_graphics_get_proc(' not in text:result+='\ndeclare i32 @tribes_graphics_get_proc(%_cpu*, i32)\n'
 return result

def prepare(text):
 proof=json.loads((R/'generated/integration-review/mobile-gpu-sync/result.json').read_text());assert proof['passed']
 for name,digest in proof['sources_sha256'].items():assert hashlib.sha256((R/name).read_bytes()).hexdigest()==digest,name
 return translate(text)
