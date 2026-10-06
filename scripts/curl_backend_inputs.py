"""Verified static backend and implemented guest curl startup bindings."""
import hashlib,importlib.util,json,pathlib
R=pathlib.Path(__file__).resolve().parents[1]
def prepare(destination):
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
 root=R/'generated/http-backend/macos';report=json.loads((root/'result.json').read_text());assert report['passed'] and report['platform']=='macos'
 assert report['config_sha256']==sha(R/'config/runtime/http-backend.json')
 libraries=[]
 for name,h in report['libraries'].items():
  assert sha(R/name)==h,name
  libraries.append(str(R/name))
 include=root/'install/include';header_hashes={}
 original=report['source_files']['ref/http-sources/curl-8.22.0']
 for p in (include/'curl').glob('*.h'):
  assert sha(p)==original['include/curl/'+p.name],p
  header_hashes[str(p.relative_to(R))]=sha(p)
 config=R/'config/runtime/curl-bindings.json';bindings=json.loads(config.read_text())
 spec=importlib.util.spec_from_file_location('curl_binding_generator',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen)
 wrapper=destination/'curl-bindings.ll';ir=gen.generate(bindings,False)
 for index,name in enumerate(bindings):
  if not bindings[name]['returns']:continue
  helper=bindings[name]['helper'];lines=ir.splitlines();matches=[line for line in lines if 'call i32 @'+helper+'(' in line];assert len(matches)==1
  ir=ir.replace(matches[0],matches[0]+'\n call void @qol_curl_result(ptr %cpu, i32 '+str(index)+', i32 %result)')
 wrapper.write_text('declare void @qol_curl_result(ptr, i32, i32)\n'+ir)
 sources=[R/'port/core/guest_curl.c',R/'port/core/guest_curl_allocator.c',wrapper]
 metadata=dict(backend_report_sha256=sha(root/'result.json'),libraries=report['libraries'],headers_sha256=header_hashes,
               bindings_sha256=sha(config),script_sha256=sha(pathlib.Path(__file__)),
               runtime_headers_sha256={n:sha(R/n) for n in ['port/core/guest_curl.h','port/core/guest_curl_allocator.h']},
               implemented_imports=list(bindings),scope='Real initialization plus reviewed socket/timer, easy lifecycle and selected option/info APIs. Socket/HTTP callback conversion has component evidence; original game callback/event execution and remaining APIs remain explicit boundaries. No gameplay claim.')
 return sources,['-I',str(include)],libraries+['-lz','-framework','SystemConfiguration','-framework','CoreFoundation','-framework','CoreServices'],bindings,metadata
