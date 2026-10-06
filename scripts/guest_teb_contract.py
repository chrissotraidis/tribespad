"""Sparse x86 diagnostic TEB identity and explicit 32-bit segment descriptors.
Not a Windows loader or native thread allocator. Caller owns mapped storage.
"""
import struct
from unicorn import x86_const as X

DECLARED_FIELDS=(0,4,8,0x18,0x20,0x24,0x2c)

def configure_sparse_teb(emulator,teb,gdt,stack_base,stack_limit,pid,tid,tls_array,allow_zero_identity=False):
    if (not teb and not allow_zero_identity) or teb%4096 or gdt%4096 or not gdt or teb==gdt:
        raise ValueError('invalid sparse TEB/GDT identity')
    if not 0<stack_limit<stack_base<=0x7fff0000 or not pid or not tid or not tls_array:
        raise ValueError('invalid sparse thread fields')
    if any(not 0<=value<=0xffffffff for value in (teb,gdt,pid,tid,tls_array)) or teb+4096>0x7fff0000 or gdt+4096>0x7fff0000:
        raise ValueError('invalid DWORD/profile field range')
    values=(0xffffffff,stack_base,stack_limit,teb,pid,tid,tls_array)
    for offset,value in zip(DECLARED_FIELDS,values):emulator.mem_write(teb+offset,struct.pack('<I',value))
    # Flat code/data descriptors are explicit: loading FS otherwise changes
    # Unicorn's default hidden SS attributes and can select a 16-bit stack.
    code=struct.pack('<HHBBBB',65535,0,0,0x9b,0xcf,0)
    data=struct.pack('<HHBBBB',65535,0,0,0x93,0xcf,0)
    fs=struct.pack('<HHBBBB',4095,teb&65535,(teb>>16)&255,0xf3,0x40,(teb>>24)&255)
    emulator.mem_write(gdt+8,code);emulator.mem_write(gdt+16,data);emulator.mem_write(gdt+0x30,fs)
    emulator.reg_write(X.UC_X86_REG_GDTR,(0,gdt,4095,0))
    for register,selector in ((X.UC_X86_REG_CS,8),(X.UC_X86_REG_DS,16),(X.UC_X86_REG_ES,16),(X.UC_X86_REG_SS,16),(X.UC_X86_REG_FS,0x33)):
        emulator.reg_write(register,selector)
    assert emulator.reg_read(X.UC_X86_REG_FS_BASE)==teb
    return {'code_descriptor_hex':code.hex(),'data_descriptor_hex':data.hex(),'fs_descriptor_hex':fs.hex(),'declared_fields':list(DECLARED_FIELDS),'fs_selector':0x33}
