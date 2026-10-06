#!/usr/bin/env python3
"""Pin observed original conversion sites and the local Intel reference."""
import hashlib,json,pathlib
import pefile,capstone
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
image=R/'ref/product/IFC22.dll';assert sha(image)=='eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607'
manual=R/'ref/intel/sdm-089.pdf';assert sha(manual)=='1eb81360c636a723fb34610a1deef276e98e1fc331ac08b380114079664add20'
m=pefile.PE(str(image)).get_memory_mapped_image();c=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);records=[]
specs=[(0x100f9f60,0x100f9f6e,'vcvttps2uqq',8,16),(0x100fa000,0x100fa00e,'vcvttps2qq',8,16),(0x100fa070,0x100fa07d,'vcvttpd2uqq',16,16),(0x100fa150,0x100fa15d,'vcvttpd2qq',16,16),(0x100fa200,0x100fa213,'vcvtuqq2pd',16,16),(0x100fa250,0x100fa263,'vcvtqq2pd',16,16),(0x100fa2a0,0x100fa2b3,'vcvtqq2ps',16,8)]
for entry,pc,name,source_bytes,result_bytes in specs:
 start=entry-0x10000000;offset=pc-0x10000000
 assert m[start:start+7]==bytes.fromhex('f605905d1d1020') and m[start+7]==0x75
 instruction=next(c.disasm(m[offset:offset+15],pc,count=1));assert instruction.mnemonic==name and instruction.op_str=='xmm0, xmm0'
 prefix=m[offset:offset+4];assert prefix[0:2]==b'\x62\xf1' and prefix[3]==8
 records.append(dict(entry=hex(entry),pc=hex(pc),bytes_hex=instruction.bytes.hex(),mnemonic=name,source_bytes=source_bytes,result_bytes=result_bytes,output_xmm_bytes=16,upper_result_bytes_zeroed=16-result_bytes,lanes=2,feature_test_va='0x101d5d90',feature_test_mask='0x20',fallback_on_bit_set=True,leading_branch_bytes_hex=m[start:offset].hex()))
out=R/'generated/analysis/ifc22/quadword-conversion-contract';out.mkdir(exist_ok=True)
paths=[pathlib.Path(__file__).resolve(),R/'scripts/extract-intel-quadword-conversions.swift',R/'generated/intel-quadword-conversions.txt']
report=dict(pass_=True,image_sha256=sha(image),manual_sha256=sha(manual),manual_url='https://cdrdv2-public.intel.com/868137/325462-089-sdm-vol-1-2abcd-3abcd-4.pdf',manual_conversion_pages=[2101,2102,2105,2106,2121,2122,2125,2126,2139,2140,2143,2144,2158,2159],source_sha256={str(p.relative_to(R)):sha(p) for p in paths},records=records,scope='Seven observed original unmasked128-bit conversion sites. Original feature byte/bit and branch direction pinned; feature initialization and optional-path reachability unproven. Two lanes,PS input uses low64bits,PS output zeros upper64bits. Manual identifies Invalid/Precision for floating-to-integer and Precision for integer-to-floating. No decoder/runtime/hardwareEVEX/fullmodule proof. Following VEX extraction/insertion support also required.')
report['pass']=report.pop('pass_');(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS: seven image-pinned conversion sites and Intel reference')
