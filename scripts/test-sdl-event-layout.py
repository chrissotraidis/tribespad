#!/usr/bin/env python3
"""Check event offsets against pinned SDL declarations compiled for Windows32."""
import hashlib,json,pathlib,re,subprocess
R=pathlib.Path(__file__).resolve().parents[1];headers=R/'ref/sdl3/SDL3-3.2.18/include/SDL3';out=R/'generated/integration-review/sdl-event-layout';out.mkdir(exist_ok=True)
clean=lambda s:re.sub(r'/\*.*?\*/','',s,flags=re.S)
h=(headers/'SDL_events.h').read_text();blocks=re.findall(r'typedef struct SDL_\w+Event\s*\{.*?\}\s*SDL_\w+Event;',h,re.S)
records=set(re.findall(r'typedef struct (SDL_\w+Event)', '\n'.join(blocks)));text='\n'.join(blocks)
types=set(re.findall(r'\bSDL_\w+\b',clean(text)))-records
all_headers=clean('\n'.join(p.read_text() for p in sorted(headers.glob('*.h'))))
aliases={b:a for a,b in re.findall(r'typedef\s+(\w+)\s+(SDL_\w+)\s*;',all_headers)}
enums=set(re.findall(r'typedef enum\b[^{]*\{.*?\}\s*(SDL_\w+)\s*;',all_headers,re.S))
prefix='typedef unsigned char Uint8;typedef unsigned short Uint16;typedef unsigned int Uint32;typedef unsigned long long Uint64;typedef signed short Sint16;typedef signed int Sint32;typedef signed long long Sint64;typedef _Bool bool;\n'
resolved={}
for t in sorted(types):
 base=t;seen=set()
 while base.startswith('SDL_'):
  assert base not in seen;seen.add(base)
  base='Uint32' if base in enums else aliases[base]
 assert base in ['Uint8','Uint16','Uint32','Uint64','Sint16','Sint32','Sint64'],(t,base)
 resolved[t]=base;prefix+='typedef '+base+' '+t+';\n'
union=re.search(r'typedef union SDL_Event\s*\{.*?\}\s*SDL_Event;',h,re.S)[0]
members={b:a for a,b in re.findall(r'(SDL_\w+Event)\s+(\w+)\s*;',union)}
backend=R/'port/core/guest_sdl_backend.c';checks=[]
for off,member,field in re.findall(r'EF\((\d+),(\w+)\.(\w+)\)',backend.read_text()):checks.append(f'_Static_assert(__builtin_offsetof({members[member]}, {field})=={off},"{member}.{field}");')
for ty,field,off in [('SDL_TextInputEvent','text',20),('SDL_TextEditingEvent','text',20),('SDL_TextEditingEvent','start',24),('SDL_TextEditingEvent','length',28),('SDL_TextEditingCandidatesEvent','candidates',20),('SDL_TextEditingCandidatesEvent','num_candidates',24),('SDL_TextEditingCandidatesEvent','selected_candidate',28),('SDL_TextEditingCandidatesEvent','horizontal',32),('SDL_DropEvent','source',28),('SDL_DropEvent','data',32),('SDL_ClipboardEvent','mime_types',24)]:checks.append(f'_Static_assert(__builtin_offsetof({ty},{field})=={off},"{ty}.{field}");')
source=out/'i686.c';source.write_text(prefix+text+'\n'+union+'\n'+'\n'.join(checks)+'\n_Static_assert(sizeof(SDL_Event)==128,"event union");\n_Static_assert(sizeof(void*)==4,"guest pointer width");\n')
cmd=['/opt/homebrew/Cellar/llvm@20/20.1.8/bin/clang','-target','i686-pc-windows-msvc','-ffreestanding','-fsyntax-only',str(source)];run=subprocess.run(cmd,capture_output=True,text=True);assert not run.returncode,run.stderr
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
(out/'result.json').write_text(json.dumps(dict(passed=True,offsets=len(checks),union_size=128,command=cmd,aliases=resolved,sources_sha256={str(p.relative_to(R)):sha(p) for p in [source,headers/'SDL_events.h',backend,pathlib.Path(__file__).resolve()]}),indent=2)+'\n');print('PASS:',len(checks),'original Windows32 event offsets and128-byte union')
