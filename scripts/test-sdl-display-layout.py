#!/usr/bin/env python3
"""Compile the pinned SDL mode declaration with the actual i686 ABI."""
import hashlib,json,pathlib,re,subprocess
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/sdl-display/layout';out.mkdir(parents=True,exist_ok=True);sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
header=R/'ref/sdl3/SDL3-3.2.18/include/SDL3/SDL_video.h';s=header.read_text();assert 'typedef Uint32 SDL_DisplayID;' in s
body=re.search(r'typedef struct SDL_DisplayMode\s*\{.*?\} SDL_DisplayMode;',s,re.S)[0]
# DisplayID and PixelFormat are32-bit values. Their native sizes are also
# checked against the real SDL headers by the executable display test.
preamble='typedef unsigned int SDL_DisplayID; typedef unsigned int SDL_PixelFormat; typedef struct SDL_DisplayModeData SDL_DisplayModeData;\n'
fields=['displayID','format','w','h','pixel_density','refresh_rate','refresh_rate_numerator','refresh_rate_denominator','internal']
checks=['_Static_assert(sizeof(SDL_DisplayMode)==36,"mode size");','_Static_assert(sizeof(void*)==4,"guest pointers");']+['_Static_assert(__builtin_offsetof(SDL_DisplayMode,%s)==%d,"%s");'%(f,i*4,f) for i,f in enumerate(fields)]
source=out/'upstream-layout.c';source.write_text(preamble+body+'\n'+'\n'.join(checks)+'\n');command=['clang','-target','i686-pc-windows-msvc','-std=c11','-fsyntax-only','-Wall','-Wextra','-Werror',str(source)];subprocess.run(command,check=True)
(out/'result.json').write_text(json.dumps(dict(passed=True,assertions=len(checks),command=command,sources_sha256={str(p.relative_to(R)):sha(p) for p in [header,source,pathlib.Path(__file__).resolve()]},scope='Unedited upstream SDL_DisplayMode declaration compiled with i686 Windows layout and32-bit scalar dependencies; not a full Windows SDL build.'),indent=2)+'\n');print('PASS: pinned SDL mode declaration,36-byte i686 Windows layout and nine field offsets')
