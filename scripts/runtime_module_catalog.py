"""Audit an explicitly bounded native guest DLL namespace; never load a DLL."""
import hashlib,pathlib

def audit_absent_module(name, roots, providers):
    assert name and name.isascii() and not any(c in name for c in '\\/:*?"<>|')
    folded=name.casefold()
    provider_names=[p.replace('\\','/').split('/')[-1].casefold() for p in providers]
    assert folded not in provider_names, 'module is defined by the host'
    inventories=[]
    for root in roots:
        root=pathlib.Path(root).resolve(strict=True);assert root.is_dir()
        paths=sorted(root.rglob('*'));names=[str(p.relative_to(root)) for p in paths]
        assert not any(p.name.casefold()==folded for p in paths), 'requested module exists in configured inputs'
        dlls=[]
        for p in paths:
            if p.suffix.casefold()=='.dll' and p.is_file():
                assert not p.is_symlink(), 'DLL symlink requires separate availability analysis'
                dlls.append({'path':str(p.relative_to(root)),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
        inventories.append({'root':str(root),'entries':len(paths),'names_sha256':hashlib.sha256('\n'.join(names).encode('utf-8')).hexdigest(),'dlls':dlls})
    return {'name':name,'absent':True,'providers':sorted(provider_names),'roots':inventories,'auditor_sha256':hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),'scope':'Configured mounted inputs and defined native/mapped providers at audit time. No Windows installation search or dynamic filesystem-update parity.'}
