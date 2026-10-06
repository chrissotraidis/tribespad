"""Build the complete numeric, single-language catalog of a pinned mapped PE."""
import hashlib

def image_resources(image):
    mapped=image.get_memory_mapped_image();size=image.OPTIONAL_HEADER.SizeOfImage
    rows=[]
    for kind in getattr(image,'DIRECTORY_ENTRY_RESOURCE',()).entries if hasattr(image,'DIRECTORY_ENTRY_RESOURCE') else []:
        assert kind.name is None and kind.id<=65535, 'Named resource types require a new contract'
        for name in kind.directory.entries:
            assert name.name is None and name.id<=65535, 'Named resources require a new contract'
            assert len(name.directory.entries)==1, 'Language selection requires a new contract'
            lang=name.directory.entries[0];entry=lang.data.struct
            rva=image.get_rva_from_offset(entry.get_file_offset())
            data=entry.OffsetToData;length=entry.Size
            assert lang.name is None and lang.id<=65535
            assert 0<rva and rva+16<=size and 0<data<size and data+length<=len(mapped)<=size
            assert mapped[rva:rva+16]==entry.__pack__()
            rows.append(dict(type=kind.id,name=name.id,language=lang.id,entry_rva=rva,data_rva=data,size=length,sha256=hashlib.sha256(mapped[data:data+length]).hexdigest()))
    assert len({(r['type'],r['name']) for r in rows})==len(rows)
    return rows

def resource_header(name,rows):
    fields=('type','name','language','entry_rva','data_rva','size')
    assert rows, 'Empty catalogs should use NULL/count=0'
    return 'static const TribesImageResource '+name+'[]={'+','.join('{'+','.join(str(r[k])+'u' for k in fields)+'}' for r in rows)+'};\n'
