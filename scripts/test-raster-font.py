#!/usr/bin/env python3
"""Validate the reached original ANSI glyph cache against actual CoreText output."""
import hashlib,importlib.util,json,pathlib,subprocess
from checked_support import compile_support
from guard_checked_link import guard_link
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/raster-font/component';out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
bindings=R/'port/llasm-runtime/win32-bindings.json';spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen)
selected=['CreateFontA','SetBkColor','SetTextColor','GetTextMetricsA','GetGlyphOutlineA','GetTextExtentPoint32A'];data=json.loads(bindings.read_text());wrapper=out/'bindings.ll';wrapper.write_text(gen.generate({k:data[k] for k in selected},False))
sources=[R/n for n in ['port/apple/font.m','port/core/gdi_memory.c','port/core/guest_heap.c','tests/fixtures/raster_font_dispatch.c','tests/fixtures/native_enter.ll']]+[wrapper]
source='source_filename = '+json.dumps(str(R/'port/apple/font.m'))+'\n'
fixtures={
 'guest':source+'''declare double @CTFontGetAscent(ptr)
define void @raster_guest_pointer(ptr %p) { %r = call double @CTFontGetAscent(ptr %p)
 ret void }
''',
 'abi':source+'''declare i64 @CTFontGetAscent(ptr)
define void @raster_wrong_abi() { %r = call i64 @CTFontGetAscent(ptr null)
 ret void }
''',
 'source':'''declare double @CTFontGetAscent(ptr)
define void @raster_unreviewed() { %r = call double @CTFontGetAscent(ptr null)
 ret void }
''',
 'convention':source+'''declare fastcc double @CTFontGetAscent(ptr)
define void @raster_wrong_convention() { %r = call fastcc double @CTFontGetAscent(ptr null)
 ret void }
''',
 'rect':source+'''%rect = type {double,double,double,double}
declare %rect @CTFontGetBoundingRectsForGlyphs(ptr,i32,ptr,ptr,i64)
define void @raster_wrong_rect() { %r = call %rect @CTFontGetBoundingRectsForGlyphs(ptr null,i32 0,ptr null,ptr null,i64 0)
 ret void }
'''}
extra=[]
for name,s in fixtures.items():p=out/('contract-'+name+'.ll');p.write_text(s);extra.append(p)
objects,_=compile_support(sources+extra,out/'checked',['-DPTROFS_64BIT']);meta=json.loads((out/'checked/result.json').read_text());entries=[]
for obj in objects:
 p=pathlib.Path(obj);row=meta['objects'][p.stem];entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=row['bitcode_sha256']))
objects,_=guard_link(entries,out/'guarded');sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip();test=R/'tests/raster_font_test.c';guards=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c'];report=dict(passed=False)
for mode in ['normal','checked','tsan']:
 exe=out/('test-'+mode);cmd=['clang','-target','arm64-apple-macos12','-isysroot',sdk,'-O1','-g','-DPTROFS_64BIT','-Wno-override-module','-Wall','-Wextra','-Werror',str(test),*map(str,guards)]
 cmd+=['-DCHECKED_TEST',*objects] if mode=='checked' else list(map(str,sources))
 if mode=='tsan':cmd+=['-fsanitize=thread']
 build=subprocess.run(cmd+['-framework','CoreText','-framework','CoreGraphics','-framework','CoreFoundation','-o',str(exe)],capture_output=True,text=True);(out/(mode+'-compile.log')).write_text(build.stdout+build.stderr);assert not build.returncode,build.stderr
 run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=120);(out/(mode+'-stdout.log')).write_text(run.stdout);(out/(mode+'-stderr.log')).write_text(run.stderr);assert not run.returncode and run.stdout.startswith('PASS:'),(mode,run.returncode,run.stdout,run.stderr)
 assert 'WARNING: ThreadSanitizer' not in run.stderr
 report[mode]=dict(passed=True,binary_sha256=sha(exe),stdout=run.stdout);print(mode,run.stdout.strip(),flush=True)
report.update(passed=True,sources_sha256={str(p.relative_to(R)):sha(p) for p in sources+[test,*guards,R/'port/apple/font.h',R/'port/core/gdi_memory.h',R/'port/compiler/ExternalMemoryPass.cpp',R/'config/runtime/external-memory-contracts.json',pathlib.Path(__file__).resolve()]},scope='Reached original upright ANSI raster-font cache with identity MAT2 and gray8. CoreText font mapping/rasterization is real; Windows typography pixel identity, arbitrary font attributes, general text drawing and full game/menu are unproven.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
