#!/usr/bin/env python3
"""Apply a Wise Installation System (16-bit, WISE0002.DLL) binary patch.

Format reversed statically from WISE0002.DLL seg1:14C0 in tribes18to111.exe.
Patch layout (little endian):
  0  u8   version (1)
  1  u16  DOS date of new file      3 u16 DOS time
  5  u32  CRC32 (zlib) of new file  9 u32 new size
  13 u32  offset of command-length field (old-version records sit in 17..it)
  17 u32  CRC32 of old file         21 u32 old size      (one 8-byte record)
  N  u32  command section length L; commands at N+4, literal bytes at N+4+L
Command (A=literal bytes, B=bytes copied from old, C=signed seek in old):
  b<0x80 : A=b>>4, B=(b&15)+3, C=0
  else   : A=varint(b&0x3f, more=b&0x40); next byte c: hasC=!(c&0x40),
           B=varint(c&0x3f, more=c&0x80)+3; if hasC: C=signed varint(d&0x7f, more=d&0x80)
           varint continuation: 7 bits, 7 bits, then a final 8-bit byte.
Apply: out+=lit[A]; old_pos+=A; if len(out)<new_size: old_pos+=C; out+=old[old_pos:+B]; old_pos+=B
Usage: decode_wisepatch.py OLD PATCH OUT
"""
import struct, sys, zlib, hashlib

class PatchError(Exception): pass

def apply(old: bytes, patch: bytes) -> bytes:
    if len(patch) < 29 or patch[0] != 1:
        raise PatchError('not a version-1 Wise patch')
    new_crc, new_size, cmd_field = struct.unpack_from('<III', patch, 5)
    recs = []
    for o in range(17, cmd_field, 8):
        recs.append(struct.unpack_from('<II', patch, o))
    if (cmd_field - 17) % 8 or len(recs) != 1:
        raise PatchError('unsupported old-version table: %r' % recs)
    old_crc, old_size = recs[0]
    if len(old) != old_size or zlib.crc32(old) != old_crc:
        raise PatchError('old file is not the version this patch expects '
                         '(want size %d crc %08x, got %d %08x)' % (old_size, old_crc, len(old), zlib.crc32(old)))
    (L,) = struct.unpack_from('<I', patch, cmd_field)
    cmds = patch[cmd_field + 4: cmd_field + 4 + L]
    lit_pos = cmd_field + 4 + L
    if len(cmds) != L:
        raise PatchError('truncated command section')
    out = bytearray(); op = 0; i = 0

    def varint(first, more, signed=False):
        nonlocal i
        v = first
        if signed and v > 0x3f: v -= 0x80
        for shift, mask in ((7, 0x7f), (7, 0x7f), (8, 0xff)):
            if not more: break
            i += 1; b = cmds[i]
            v = (v << shift) | (b & mask)
            more = b & 0x80 if mask == 0x7f else 0
        return v

    while len(out) < new_size:
        if i >= L: raise PatchError('command stream ended at output %d' % len(out))
        b = cmds[i]
        if b & 0x80:
            A = varint(b & 0x3f, b & 0x40); i += 1
            c = cmds[i]; hasC = not (c & 0x40)
            B = varint(c & 0x3f, c & 0x80) + 3; i += 1
            C = 0
            if hasC:
                d = cmds[i]; C = varint(d & 0x7f, d & 0x80, signed=True); i += 1
        else:
            A, B, C = b >> 4, (b & 15) + 3, 0; i += 1
        if A:
            chunk = patch[lit_pos:lit_pos + A]
            if len(chunk) != A: raise PatchError('literal data exhausted')
            out += chunk; lit_pos += A; op += A
        if len(out) < new_size:
            op += C
            if op < 0 or op + B > len(old): raise PatchError('copy outside old file at cmd %d' % i)
            out += old[op:op + B]; op += B
    if i != L or lit_pos != len(patch):
        raise PatchError('unconsumed patch data (cmd %d/%d, lit %d/%d)' % (i, L, lit_pos, len(patch)))
    if len(out) != new_size or zlib.crc32(out) != new_crc:
        raise PatchError('output check failed: size %d crc %08x, want %d %08x' % (len(out), zlib.crc32(out), new_size, new_crc))
    return bytes(out)

if __name__ == '__main__':
    old_p, patch_p, out_p = sys.argv[1:4]
    try:
        data = apply(open(old_p, 'rb').read(), open(patch_p, 'rb').read())
    except PatchError as e:
        sys.exit('decode_wisepatch: FAILED: %s' % e)
    open(out_p, 'wb').write(data)
    print('%s  %d bytes  crc32 %08x  sha256 %s' % (out_p, len(data), zlib.crc32(data), hashlib.sha256(data).hexdigest()))
