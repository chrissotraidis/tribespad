"""Validate observed original register-IAT call sites; no inferred scan roots."""
import hashlib
IMAGE_SHA256='eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607'
# Original straight-line span: MOV ESI,[curl_multi_setopt IAT], socket setup,
# CALL ESI, caller cleanup, timer setup, CALL ESI. ESI is callee-saved; its
# captured value must also equal the loader's exact import token at the stop.
START=0x10023c1a
PREFIX=bytes.fromhex('8b351097121068c017021068214e000050ffd683c40c681017021068244e0000ff35b0df4610ffd6')
RETURNS={0x10023c2d:19,0x10023c42:40}
# Reached original SDL window/display lookup: immediate MOV ESI,[IAT], CALL ESI.
WINDOW_START=0x10049fff
WINDOW_PREFIX=bytes.fromhex('8b358c951210ffd6')
def verify(pe,image_bytes,row,return_pc,registers):
    # Pinned Miles string-building span: ESI retains lstrcatA's IAT token
    # across the optional first append and the final append. The conditional
    # jump joins at 21112142 and no path assigns ESI after its IAT load.
    if hashlib.sha256(image_bytes).hexdigest()=='cd3c9b26cd1f4e7037bac56056427908c2ef8dc9fb6f899ab9f47bd8046ba9fb':
        start=0x2111212d;prefix=bytes.fromhex('8b359cf01421c6070085d2740868d0c1152157ffd68d45d45057ffd6')
        lengths={0x21112142:21,0x21112149:28};length=lengths.get(return_pc)
        if length and row.get('dll','').lower()=='kernel32.dll' and row.get('name')=='lstrcatA' and row.get('slot')==0x2114f09c and row.get('guest_va') is not None and registers.get('esi')==row['guest_va']:
            span=pe.get_data(start-pe.OPTIONAL_HEADER.ImageBase,length)
            if span==prefix[:length]:return dict(kind='register_iat',register='esi',register_value=row['guest_va'],iat_slot=row['slot'],load_pc=start,verified_span=span.hex(),call_pc=return_pc-2,call_bytes='ffd6')
        return None
    # Exact adjacent MOV r32,[absolute IAT]; CALL same r32. This is one bounded
    # eight-byte encoding, not a backward scan or a new callable target.
    if hashlib.sha256(image_bytes).hexdigest()!=IMAGE_SHA256:return None
    rva=return_pc-8-pe.OPTIONAL_HEADER.ImageBase
    span=pe.get_data(rva,8) if rva>=0 else b''
    if len(span)==8 and span[0]==0x8b and span[1]&0xc7==5 and span[6]==0xff:
        reg=(span[1]>>3)&7;slot=int.from_bytes(span[2:6],'little');name=['eax','ecx','edx','ebx','esp','ebp','esi','edi'][reg]
        actual=[(entry.dll.decode().lower(),imp.name.decode() if imp.name else None) for entry in pe.DIRECTORY_ENTRY_IMPORT for imp in entry.imports if imp.address==slot]
        token=row.get('guest_va')
        if span[7]==0xd0+reg and slot==row.get('slot') and actual==[(row.get('dll','').lower(),row.get('name'))] and token is not None and registers.get(name)==token:
            return dict(kind='register_iat',register=name,register_value=token,iat_slot=slot,load_pc=return_pc-8,verified_span=span.hex(),call_pc=return_pc-2,call_bytes=span[6:].hex())

    # Pinned cursor builder: MOV ESI,[SDL_CreateSurfaceFrom IAT], optional
    # scaling, then either CALL ESI. Branches join inside this exact span;
    # no path assigns ESI again, and intervening cdecl calls preserve it.
    cursor_spans={268638694: 'e6e852e4978114ad8d8551cfe4f09b94d49b4ee2215b4204669bf596e11667f0', 268638743: '3380448a85cb9c436955d5bd6943c4f98d3723d645853e600b7564466f4f2beb'}
    if return_pc in cursor_spans:
        token=row.get('guest_va');start=0x1003191b
        original=pe.get_data(start-pe.OPTIONAL_HEADER.ImageBase,return_pc-start)
        if row.get('dll','').lower()=='sdl3.dll' and row.get('name')=='SDL_CreateSurfaceFrom' and row.get('slot')==0x101294f0 and token is not None and registers.get('esi')==token and hashlib.sha256(original).hexdigest()==cursor_spans[return_pc]:
            return dict(kind='register_iat',register='esi',register_value=token,iat_slot=row['slot'],load_pc=start,verified_span=original.hex(),call_pc=return_pc-2,call_bytes='ffd6')
        return None

    if return_pc==WINDOW_START+len(WINDOW_PREFIX):
        if hashlib.sha256(image_bytes).hexdigest()!=IMAGE_SHA256:return None
        if row.get('dll','').lower()!='sdl3.dll' or row.get('name')!='SDL_GetDisplayForWindow' or row.get('slot')!=0x1012958c:return None
        token=row.get('guest_va')
        if token is None or registers.get('esi')!=token:return None
        original=pe.get_data(WINDOW_START-pe.OPTIONAL_HEADER.ImageBase,len(WINDOW_PREFIX))
        if original!=WINDOW_PREFIX:return None
        return dict(kind='register_iat',register='esi',register_value=token,iat_slot=row['slot'],
                    load_pc=WINDOW_START,verified_span=original.hex(),call_pc=return_pc-2,call_bytes='ffd6')
    length=RETURNS.get(return_pc)
    if length is None or hashlib.sha256(image_bytes).hexdigest()!=IMAGE_SHA256:return None
    if row.get('dll','').lower()!='libcurl.dll' or row.get('name')!='curl_multi_setopt' or row.get('slot')!=0x10129710:return None
    token=row.get('guest_va')
    if token is None or registers.get('esi')!=token:return None
    original=pe.get_data(START-pe.OPTIONAL_HEADER.ImageBase,length)
    if original!=PREFIX[:length] or START+length!=return_pc:return None
    return dict(kind='register_iat',register='esi',register_value=token,iat_slot=row['slot'],
                load_pc=START,verified_span=original.hex(),call_pc=return_pc-2,call_bytes='ffd6')
