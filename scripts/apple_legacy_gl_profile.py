"""Source-bound Mac legacy-GL data adaptation; original instructions are unchanged."""
import hashlib
import pathlib
import struct

IMAGE_SHA256='eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607'
sha=lambda value:hashlib.sha256(value).hexdigest()

def prepare(image_path,mapped):
    import pefile
    raw=image_path.read_bytes()
    assert sha(raw)==IMAGE_SHA256,'unreviewed IFC22 image'
    pe=pefile.PE(data=raw)
    assert pe.OPTIONAL_HEADER.ImageBase==0x10000000
    assert mapped==pe.get_memory_mapped_image(),'expected pristine mapped IFC22 data'
    address=0x101c4b4c;offset=address-0x10000000
    before=b'GL_ARB_timer_query\0';after=b'GL_EXT_timer_query\0'
    assert len(before)==len(after) and mapped[offset:offset+len(before)]==before
    # Its sole original consumer gates loading GetQueryObjectui64v. The other
    # two queried entry points (QueryCounter/i64v) are discarded, not stored.
    consumer=bytes.fromhex('b94c4b1c10a384c74610e890ddffffb9604b1c10')
    resolver=bytes.fromhex('833db0c7461000741d6878f41b10ffd66888f41b10ffd668a0f41b10ffd683c40ca38cc84610')
    assert mapped[0x1a4e1:0x1a4e1+len(consumer)]==consumer
    assert mapped[0x1c00d:0x1c00d+len(resolver)]==resolver
    refs=sum(pe.get_data(s.VirtualAddress,s.Misc_VirtualSize).count(struct.pack('<I',address)) for s in pe.sections if s.Characteristics&0x20000000)
    assert refs==1
    section=pe.get_section_by_rva(offset)
    assert section and section.Characteristics&0x40000000 and not section.Characteristics&0xa0000000
    result=mapped[:offset]+after+mapped[offset+len(before):]
    assert [i for i,(a,b) in enumerate(zip(mapped,result)) if a!=b]==list(range(offset+3,offset+6))
    return result,dict(image_sha256=IMAGE_SHA256,source_sha256=sha(pathlib.Path(__file__).read_bytes()),mapped_before_sha256=sha(mapped),mapped_after_sha256=sha(result),address=hex(address),before=before[:-1].decode(),after=after[:-1].decode(),consumer='0x1001a4e1',resolver='0x1001c00d',scope='Mac GL2.1 elapsed-time result loader uses actual EXT_timer_query support. Native extension strings, original instructions and timestamp capability remain unchanged.')
