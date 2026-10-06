"""Verify the original startup DirectInput provider-string edit in PE data.

The guest requests RWX, but Apple backing remains nonexecuting. This metadata
admits only the source-bound data page and validates its data-only cache flush.
It does not authorize the adjacent engine code patch.
"""
import hashlib,json,pathlib
import pefile
R=pathlib.Path(__file__).resolve().parents[1]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def prepare(out):
 engine=R/'ref/control/Tribes2.exe';qol=R/'ref/product/IFC22.dll'
 assert sha(engine)=='7ad1d40a6aa74a25d089af6bd6f34fcc20a172fd5932ded82c69fca673e7f8f9'
 assert sha(qol)=='eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607'
 pe=pefile.PE(str(engine));dll=pefile.PE(str(qol));address=0x7868d0;length=10;page=address&~4095
 section=pe.get_section_by_rva(address-0x400000)
 assert section.Name.rstrip(b'\0')==b'.data' and section.Characteristics&0x80000000 and not section.Characteristics&0x20000000
 assert pe.get_data(address-0x400000,length)==b'DInput.dll'
 assert dll.get_data(0x1ca77c,length)==b'IFC22.dll\0'
 # Original VirtualProtect request followed by the two stores and cache flush.
 instructions={0x10060c4b:'8d442410506a406a0a68d0687800ffd6',0x10060c5f:'f30f7e057ca71c10',0x10060c67:'b9d06878006a0a51660fd601',0x10060c73:'66a184a71c1066894108ff156490121050ff157c901210'}
 for pc,raw in instructions.items():assert dll.get_data(pc-0x10000000,len(bytes.fromhex(raw))).hex()==raw,hex(pc)
 output=out/'startup_input_data.inc';output.write_text('''/* Verified original .data patch; native backing stays nonexecuting. */
#define TB_INPUT_DATA_ADDRESS 0x7868d0u
#define TB_INPUT_DATA_BYTES 10u
static const unsigned char input_data_before[10]={'D','I','n','p','u','t','.','d','l','l'};
static const unsigned char input_data_after[10]={'I','F','C','2','2','.','d','l','l',0};
''')
 return dict(data_address=address,data_size=length,image_page=page,engine_sha256=sha(engine),qol_sha256=sha(qol),instructions=instructions,original_hex=b'DInput.dll'.hex(),replacement_hex=b'IFC22.dll\0'.hex(),source_sha256=sha(pathlib.Path(__file__).resolve()),include_sha256=sha(output),scope='One original provider-name update in a writable non-code section. Native backing remains nonexecuting; code publication rules are unchanged.')
