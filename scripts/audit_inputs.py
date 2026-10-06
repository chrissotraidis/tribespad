#!/usr/bin/env python3
"""Inventory actual PE inputs without running them; no compatibility claims from names."""
import hashlib, json, pathlib, re, datetime, argparse
import pefile
ROOT = pathlib.Path(__file__).resolve().parents[1]

def digest(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''): h.update(b)
    return h.hexdigest()

def inspect(p):
    pe=pefile.PE(str(p))
    imports=[]
    for kind,delay in [('DIRECTORY_ENTRY_IMPORT',False),('DIRECTORY_ENTRY_DELAY_IMPORT',True)]:
        for d in getattr(pe,kind,[]):
            imports.append({'module':d.dll.decode(),'delay':delay,'functions':[i.name.decode() if i.name else '#'+str(i.ordinal) for i in d.imports]})
    versions={}
    for group in getattr(pe,'FileInfo',[]):
        for entry in group:
            for table in getattr(entry,'StringTable',[]): versions.update({k.decode():v.decode(errors='replace') for k,v in table.entries.items()})
    tls=[]
    if hasattr(pe,'DIRECTORY_ENTRY_TLS'):
        import struct
        image=pe.get_memory_mapped_image(); pos=pe.DIRECTORY_ENTRY_TLS.struct.AddressOfCallBacks-pe.OPTIONAL_HEADER.ImageBase
        while 0<=pos<=len(image)-4:
            addr=struct.unpack_from('<I',image,pos)[0]
            if not addr: break
            tls.append(hex(addr)); pos+=4
    return {'path':str(p.relative_to(ROOT)),'sha256':digest(p),'bytes':p.stat().st_size,'machine':hex(pe.FILE_HEADER.Machine),'base':hex(pe.OPTIONAL_HEADER.ImageBase),'entry':hex(pe.OPTIONAL_HEADER.ImageBase+pe.OPTIONAL_HEADER.AddressOfEntryPoint),'image_size':pe.OPTIONAL_HEADER.SizeOfImage,'version':versions,'signature_present':bool(pe.OPTIONAL_HEADER.DATA_DIRECTORY[4].Size),'signature_verified':False,'imports':imports,'tls_callbacks':tls,'exports':[{'name':e.name.decode(errors='replace') if e.name else '#'+str(e.ordinal),'address':hex(pe.OPTIONAL_HEADER.ImageBase+e.address)} for e in getattr(getattr(pe,'DIRECTORY_ENTRY_EXPORT',None),'symbols',[])],'dynamic_module_candidates':sorted({s.decode(errors='replace') for s in re.findall(rb'[a-zA-Z0-9_.-]+\.dll',p.read_bytes(),re.I)})}

def main():
    modules=[]
    for folder in ['ref/control','ref/qol']:
        for p in sorted((ROOT/folder).glob('*')):
            if p.is_file() and p.suffix.lower() in ['.exe','.dll','.asi','.m3d','.flt']:
                try: modules.append(inspect(p))
                except pefile.PEFormatError: pass
    out=ROOT/'generated/inputs-audit.json';out.write_text(json.dumps({'schema':1,'generated_at':datetime.datetime.now(datetime.timezone.utc).isoformat(),'modules':modules,'limitations':['Static imports and string candidates do not prove runtime reachability or implementation compatibility.','Authenticode presence is not signature validation.','IFC22 code writes and generated trampolines require dynamic capture.']},indent=2)+'\n')
    for m in modules: print(f"{m['path']}: {m['version'].get('FileVersion','no version')} {m['bytes']} bytes, {sum(len(x['functions']) for x in m['imports'])} imports")
if __name__=='__main__': main()
