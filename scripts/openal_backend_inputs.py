"""Finite cdecl OpenAL 1.1 routes backed by the pinned native CoreAudio mixer."""
import hashlib,importlib.util,json,pathlib
R=pathlib.Path(__file__).resolve().parents[1]
# (name, original DWORD argument count, EAX return); floats return via guest x87.
EXPORTS=[]
def add(names,n,returns=False):
 for name in names.split():EXPORTS.append((name,n,returns))
add('alGetError alcGetCurrentContext',0,True)
add('alEnable alDisable alSourcePlay alSourcePause alSourceStop alSourceRewind alDistanceModel alSpeedOfSound alDopplerFactor alcDestroyContext',1)
add('alIsEnabled alGetBoolean alGetInteger alGetString alGetEnumValue alGetProcAddress alIsSource alIsBuffer alIsExtensionPresent alcOpenDevice alcCloseDevice alcMakeContextCurrent alcGetError alcGetContextsDevice',1,True)
add('alGetFloat alGetDouble',1)
add('alGetBooleanv alGetIntegerv alGetFloatv alGetDoublev alListenerf alListenerfv alGetListeneriv alGetListenerfv alGenSources alDeleteSources alGenBuffers alDeleteBuffers alSourcePlayv alSourceStopv',2)
add('alcCreateContext alcIsExtensionPresent alcGetString alcGetProcAddress',2,True)
add('alSourcei alSourcef alSourcefv alGetSourceiv alGetSourcefv alBufferi alGetBufferi alGetBufferf alSourceQueueBuffers alSourceUnqueueBuffers',3)
add('alListener3f alcGetIntegerv',4)
add('alSource3f alSource3i alGetSource3f alBufferData',5)
# Append routes to retain every previously published core address.
add('alSourceiv',3)
for family in ['Effect','Filter','AuxiliaryEffectSlot']:
 add('alGen'+family+'s alDelete'+family+'s',2)
 add('alIs'+family,1,True)
 add(' '.join('al'+family+x for x in ['i','iv','f','fv'])+' '+' '.join('alGet'+family+x for x in ['i','iv','f','fv']),3)
assert len({x[0] for x in EXPORTS})==len(EXPORTS)
def inputs():
 meta=json.loads((R/'generated/openal-soft/macos/result.json').read_text());lib=R/meta['library']
 assert hashlib.sha256(lib.read_bytes()).hexdigest()==meta['library_sha256']
 return meta,['-DAL_LIBTYPE_STATIC','-I',str(R/'ref/openal-soft/include')],[str(lib),'-lc++','-framework','CoreAudio','-framework','AudioUnit','-framework','ApplicationServices','-framework','AudioToolbox']
def prepare(out):
 bindings={name:dict(helper='tribes_oa_'+name,arguments=n,returns=ret,helper_returns='i32',guest_base=False,cpu_argument=True,calling_convention='cdecl') for name,n,ret in EXPORTS}
 spec=importlib.util.spec_from_file_location('oa_bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen)
 wrapper=out/'openal-bindings.ll';wrapper.write_text(gen.generate(bindings,False))
 table='/* Generated finite OpenAL routes. */\n'+''.join('extern void tbimp_'+name+'(void);\n' for name,_,_ in EXPORTS)
 table+='static const OpenALExport openal_exports[]={\n'+''.join(' {"%s",0x%xu,(void *)tbimp_%s},\n'%(name,0xffd10000+16*i,name) for i,(name,_,_) in enumerate(EXPORTS))+'};\n'
 (out/'openal-exports.inc').write_text(table)
 return [R/'port/core/guest_openal.c',wrapper],bindings
