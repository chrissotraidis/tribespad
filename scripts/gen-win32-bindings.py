#!/usr/bin/env python3
"""Generate original-address stdcall/cdecl wrappers for implemented native services."""
import json,pathlib
R=pathlib.Path(__file__).resolve().parents[1]
def generate(bindings, include_unsupported_dinput=True):
 declared={}
 lines=['; Generated stdcall/cdecl wrappers: original guest PCs, preserved CPU prefix.','declare ptr @tribes_lookup(i32)']
 for name,info in bindings.items():
  n=info['arguments'];convention=info.get('calling_convention','stdcall');assert convention in ('stdcall','cdecl');base=info['guest_base'];cpu=info.get('cpu_argument',False);assert not (cpu and base);result_type=info.get('helper_returns','i32');assert result_type in ['i32','void'];assert result_type!='void' or not info['returns'];types=(['ptr'] if cpu else ['i64'] if base else [])+['i32']*n
  never=info.get('noreturn',False);assert not never or result_type=='void'
  signature=(result_type,tuple(types),never)
  if info['helper'] in declared:assert declared[info['helper']]==signature,'Incompatible native helper aliases'
  else:
   declared[info['helper']]=signature;lines+=['declare %s @%s(%s)%s'%(result_type,info['helper'],', '.join(types),' noreturn' if never else '')]
  lines+=['define hidden fastcc void @tbimp_%s(ptr %%cpu) {'%name,' %sp = getelementptr i8, ptr %cpu, i64 16',' %esp = load i32, ptr %sp',' %basep = getelementptr i8, ptr %cpu, i64 112',' %base = load i64, ptr %basep',' %offset = zext i32 %esp to i64',' %addr = add i64 %base, %offset',' %retp = inttoptr i64 %addr to ptr',]
  if not never:lines+=[' %target = load i32, ptr %retp, align 1']
  for i in range(n):lines+=[' %%argp%d = getelementptr i8, ptr %%retp, i64 %d'%(i,4*(i+1)),' %%arg%d = load i32, ptr %%argp%d, align 1'%(i,i)]
  args=(['ptr %cpu'] if cpu else ['i64 %base'] if base else [])+['i32 %%arg%d'%i for i in range(n)]
  lines+=[(' %%result = call i32 @%s(%s)' if result_type=='i32' else ' call void @%s(%s)')%(info['helper'],', '.join(args))]
  if never:
   lines+=[' unreachable','}'];continue
  if info['returns']:lines+=[' store i32 %result, ptr %cpu']
  lines+=[' %%next = add i32 %%esp, %d'%(4*(n+1) if convention=='stdcall' else 4),' store i32 %next, ptr %sp',' %fn = call ptr @tribes_lookup(i32 %target)',' musttail call fastcc void %fn(ptr %cpu)',' ret void','}']
 if not include_unsupported_dinput:return '\n'.join(lines)+'\n'
 # Unsupported native COM methods preserve the entry context and stop.
 lines+=['declare void @tribes_unimplemented_dinput(ptr, i32) noreturn']
 for index,name in [(3,'CreateDevice'),(4,'EnumDevices'),(5,'GetDeviceStatus'),(6,'RunControlPanel'),(8,'FindDevice')]:
  if 'DI_'+name in bindings:continue
  lines+=['define hidden fastcc void @tbimp_DI_%s(ptr %%cpu) {'%name,' call void @tribes_unimplemented_dinput(ptr %%cpu, i32 %d)'%index,' unreachable','}']
 for index,name in enumerate(['QueryInterface', 'AddRef', 'Release', 'GetCapabilities', 'EnumObjects', 'GetProperty', 'SetProperty', 'Acquire', 'Unacquire', 'GetDeviceState', 'GetDeviceData', 'SetDataFormat', 'SetEventNotification', 'SetCooperativeLevel', 'GetObjectInfo', 'GetDeviceInfo', 'RunControlPanel', 'Initialize', 'CreateEffect', 'EnumEffects', 'GetEffectInfo', 'GetForceFeedbackState', 'SendForceFeedbackCommand', 'EnumCreatedEffectObjects', 'Escape', 'Poll', 'SendDeviceData']):
  if 'DID_'+name in bindings:continue
  lines+=['define hidden fastcc void @tbimp_DID_%s(ptr %%cpu) {'%name,' call void @tribes_unimplemented_dinput(ptr %%cpu, i32 %d)'%(100+index),' unreachable','}']
 return '\n'.join(lines)+'\n' 
if __name__=='__main__':print(generate(json.loads((R/'port/llasm-runtime/win32-bindings.json').read_text())),end='')
