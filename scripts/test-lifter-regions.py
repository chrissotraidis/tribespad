#!/usr/bin/env python3
"""Exercise the actual SRW region partitioner with code and retained data."""
import hashlib
import json
import os
import pathlib
import subprocess
from sse_instruction_fixture import fixture

ROOT = pathlib.Path(__file__).resolve().parents[1]
source = ROOT / 'generated/tools/SRW-tribes/SR_full.c'
text = source.read_text()
begin = text.index('static void SR_insert_instruction_regions(')
end = text.index('\nstatic void SR_disassemble_create_regions(', begin)
out = ROOT / 'generated/integration-review/regions'
out.mkdir(parents=True, exist_ok=True)
harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
enum {OT_UNKNOWN,OT_OFFSET,OT_INSTRUCTION,OT_NONE};
typedef struct {uint_fast32_t ofs,len;int type;const char *str;} output_data;
static output_data items[]={
 {0,1,OT_INSTRUCTION,"mov eax, eax"}, {2,0,OT_INSTRUCTION,"ret"},
 {3,3,OT_OFFSET,"daddr loc_10001020"},
 {7,3,OT_OFFSET,"daddr loc_10001030"},
 {11,0,OT_UNKNOWN,0}, {16,4,OT_INSTRUCTION,"mov eax, 1"},
 {21,0,OT_INSTRUCTION,"ret"}, {32,0,OT_INSTRUCTION,"ret"},
 {33,0,OT_NONE,0}};
static unsigned count;
static uint_fast32_t ranges[12][2];
static output_data *section_output_list_FindEntryEqualOrHigher(unsigned e,uint_fast32_t x){assert(e==0);for(unsigned i=0;i<sizeof items/sizeof items[0];++i)if(items[i].ofs>=x)return &items[i];return 0;}
static output_data *section_output_list_FindEntryHigher(unsigned e,uint_fast32_t x){return section_output_list_FindEntryEqualOrHigher(e,x+1);}
static void section_region_list_Insert(unsigned e,uint_fast32_t a,uint_fast32_t b){assert(e==0&&count<12);ranges[count][0]=a;ranges[count++][1]=b;}
'''
harness += text[begin:end]
harness += r'''
int main(void){
 output_data before[sizeof items/sizeof items[0]];memcpy(before,items,sizeof items);
 SR_insert_instruction_regions(0,0,33);
 assert(count==3&&ranges[0][0]==0&&ranges[0][1]==2&&ranges[1][0]==16&&ranges[1][1]==21&&ranges[2][0]==32&&ranges[2][1]==32);
 count=0;SR_insert_instruction_regions(0,3,11);assert(count==0);
 SR_insert_instruction_regions(0,2,7);assert(count==1&&ranges[0][0]==2&&ranges[0][1]==2);
 count=0;SR_insert_instruction_regions(0,16,21);assert(count==1&&ranges[0][0]==16&&ranges[0][1]==21);
 assert(!memcmp(before,items,sizeof items));
 puts("PASS: contiguous code, literal tables, padding, address gaps and bounded regions; all output items retained");
}
'''
(out / 'regions.c').write_text(harness)
subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', str(out / 'regions.c'),
                '-o', str(out / 'regions')], check=True)
run = subprocess.run([str(out / 'regions')], capture_output=True, text=True, check=True)
assert not run.stderr
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
srw = ROOT / 'generated/tools/SRW-tribes/SRW.exe'
negative = []
for name, raw in [('address16_shift', '67660fd3c8'), ('scalar_compare', 'f20fc2c601')]:
    directory = out / name
    strict = fixture(directory, [{'bytes_hex': raw}], ROOT / 'ref/product/IFC22.dll', srw)
    assert strict.returncode != 0 and 'result 8' in strict.stderr
    diagnostic = subprocess.run([str(srw), 'fixture.exe', 'diagnostic.llasm'],
                                cwd=directory, capture_output=True, text=True,
                                env={**os.environ, 'TRIBES_SRW_FULL_FLAGS': '1',
                                     'HALOPAD_SRW_DIAG': '1'}, timeout=60)
    assert diagnostic.returncode == 0, diagnostic.stderr
    assert 'HALOPAD-DIAG lowering-error 0x10001000 result 8' in diagnostic.stderr
    assert 'call halopad_trap_unimplemented 0x10001000' in (directory / 'seg01_code.llinc').read_text()
    (directory / 'diagnostic.log').write_text(diagnostic.stdout + diagnostic.stderr)
    negative.append({'name': name, 'bytes_hex': raw, 'strict_exit': strict.returncode,
                     'diagnostic_exit': diagnostic.returncode})
(out / 'result.json').write_text(json.dumps({
    'pass': True, 'source_sha256': sha(source),
    'driver_sha256': sha(pathlib.Path(__file__).resolve()),
    'binary_sha256': sha(out / 'regions'), 'output': run.stdout.strip(),
    'strict_negative_forms': negative, 'srw_sha256': sha(srw),
    'scope': 'Actual partition function under ASan/UBSan with independently specified '
             'region boundaries and unchanged input/data objects. Full-image census '
             'and engine regression are separate checks.'
}, indent=2) + '\n')
print(run.stdout.strip())
