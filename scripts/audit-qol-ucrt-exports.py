#!/usr/bin/env python3
"""Inventory the selected QoL runtime's adjacent export names, not API execution."""
import hashlib,json,pathlib,re,struct
import pefile
R=pathlib.Path(__file__).resolve().parents[1];path=R/'ref/product/IFC22.dll';blob=path.read_bytes();sha=hashlib.sha256(blob).hexdigest();assert sha=='eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607';p=pefile.PE(data=blob);raw=p.get_memory_mapped_image();base=p.OPTIONAL_HEADER.ImageBase
first=raw.index(b'AreFileApisANSI\0');last=raw.index(b'AppPolicyGetThreadInitializationType\0',first)+len(b'AppPolicyGetThreadInitializationType\0');bindings=json.loads((R/'port/llasm-runtime/win32-bindings.json').read_text());trace=json.loads((R/'generated/qol-initialization/executable-capture-no-sse2.json').read_text());rows=[]
for m in re.finditer(rb'[A-Za-z][A-Za-z0-9]+\0',raw[first:last]):
 name=m.group()[:-1].decode('ascii');offset=first+m.start();rows.append({'name':name,'guest_string_address':base+offset,'preceding_8_bytes_hex':raw[offset-8:offset].hex(),'native_binding':bindings.get(name),'resolved_in_current_capture':any(s['name']=='GetProcAddress' and s['value']==name and s['eax'] for s in trace['service_calls']),'invoked_in_current_capture':any(s['name']==name for s in trace['service_calls'])})
assert len(rows)==16,len(rows)
report={'state':'finished','qol_sha256':sha,'script_sha256':hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),'trace_sha256':hashlib.sha256((R/'generated/qol-initialization/executable-capture-no-sse2.json').read_bytes()).hexdigest(),'bindings_sha256':hashlib.sha256((R/'port/llasm-runtime/win32-bindings.json').read_bytes()).hexdigest(),'guest_data_range':[base+first,base+last],'names':rows,'current_boundary':trace['stages'][-1]['stop'],'scope':'Static adjacent runtime export-name candidates plus actual capture observations. Presence does not prove invocation, mandatory initialization use, exact CRT version or metadata meaning. No missing function published or synthetic result supplied.'}
out=R/'generated/qol-runtime-export-inventory.json';out.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
