"""Finite offline vector model for pinned compression routine ranges.

Scalar control flow executes in Unicorn. This model is diagnostic Python, not
an app interpreter or a claim of hardware AVX512 execution.
"""
import struct
import capstone
from unicorn import x86_const as X

class VectorReference:
    def __init__(self, uc, ranges=None):
        self.uc = uc
        self.v = [bytes(64) for _ in range(8)]
        self.k = [0] * 8
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.md.detail = True
        self.cache = {}
        self.sites = {}
        self.tail_calls = 0
        self.ranges = ranges or [(0x100c7520, 0x100c7732)]

    def address(self, ins, op):
        m = op.mem
        assert not m.segment
        def reg(r):
            return self.uc.reg_read(getattr(X, 'UC_X86_REG_' + ins.reg_name(r).upper())) if r else 0
        return (m.disp + reg(m.base) + reg(m.index) * m.scale) & 0xffffffff

    def read(self, ins, op, size=None):
        n = size or op.size
        if op.type == capstone.x86_const.X86_OP_MEM:
            return bytes(self.uc.mem_read(self.address(ins, op), n))
        assert op.type == capstone.x86_const.X86_OP_REG
        name = ins.reg_name(op.reg)
        if name.startswith(('xmm', 'ymm', 'zmm')):
            return self.v[int(name[3:])][:n]
        value = self.uc.reg_read(getattr(X, 'UC_X86_REG_' + name.upper()))
        return int(value & ((1 << (8*n))-1)).to_bytes(n, 'little')

    def write(self, ins, op, value):
        if op.type == capstone.x86_const.X86_OP_MEM:
            self.uc.mem_write(self.address(ins, op), value); return
        name = ins.reg_name(op.reg)
        if name.startswith(('xmm', 'ymm', 'zmm')):
            assert len(value) == op.size
            self.v[int(name[3:])] = value + bytes(64 - len(value)); return
        self.uc.reg_write(getattr(X, 'UC_X86_REG_' + name.upper()), int.from_bytes(value, 'little'))

    def hook(self, uc, pc, size, unused):
        if pc == 0x100c5ae0:
            # Explicit tail substitution: the original AVX2 tail cannot execute
            # in Unicorn. Positive tails are checksum-only and <64 bytes. The
            # original scalar implementation supplies their Adler contract.
            length = struct.unpack('<I', uc.mem_read(uc.reg_read(X.UC_X86_REG_ESP) + 12, 4))[0]
            assert 0 < length < 64
            self.tail_calls += 1
            uc.reg_write(X.UC_X86_REG_EIP, 0x100c1c20); return
        if bytes(uc.mem_read(pc, 1)) not in (b'\xc4', b'\xc5', b'\x62'): return
        assert any(start <= pc < end for start, end in self.ranges), hex(pc)
        if pc not in self.cache:
            self.cache[pc] = next(self.md.disasm(bytes(uc.mem_read(pc, 15)), pc, count=1))
        i = self.cache[pc]; op = i.operands; mnemonic = i.mnemonic
        self.sites.setdefault(pc, {'bytes_hex': bytes(i.bytes).hex(), 'mnemonic': mnemonic, 'operands': i.op_str, 'count': 0})['count'] += 1
        if mnemonic == 'vzeroupper':
            self.v = [v[:16] + bytes(48) for v in self.v]
        elif mnemonic == 'shrx':
            value = int.from_bytes(self.read(i, op[1], 4), 'little')
            count = int.from_bytes(self.read(i, op[2], 4), 'little') & 31
            self.write(i, op[0], (value >> count).to_bytes(4, 'little'))
        elif mnemonic in ['vpternlogq','vpternlogd']:
            assert len(op) == 4 and op[0].size in [16,32,64]
            a,b,c = [self.read(i,o) for o in op[:3]]
            control = op[3].imm
            result = bytes(sum(((control >> (((x>>bit&1)<<2)|((y>>bit&1)<<1)|(z>>bit&1)))&1)<<bit
                               for bit in range(8)) for x,y,z in zip(a,b,c))
            self.write(i, op[0], result)
        elif mnemonic == 'vpclmulqdq':
            assert len(op) == 4
            a = self.read(i,op[1]); b = self.read(i,op[2]); control = op[3].imm
            result = bytearray()
            for start in range(0,op[0].size,16):
                x = int.from_bytes(a[start+8*(control&1):start+8*(control&1)+8],'little')
                y = int.from_bytes(b[start+8*((control>>4)&1):start+8*((control>>4)&1)+8],'little')
                product = 0
                while x:
                    bit=x&-x; product ^= y << (bit.bit_length()-1); x ^= bit
                result += product.to_bytes(16,'little')
            self.write(i,op[0],bytes(result))
        elif mnemonic == 'vinserti32x4':
            assert len(op)==4 and op[0].size in [32,64]
            value=bytearray(self.read(i,op[1]));start=(op[3].imm&(op[0].size//16-1))*16
            value[start:start+16]=self.read(i,op[2],16)
            self.write(i,op[0],bytes(value))
        elif mnemonic == 'vextracti32x4':
            assert len(op)==3 and op[0].size==16 and op[1].size in [32,64]
            start=(op[2].imm&(op[1].size//16-1))*16
            self.write(i,op[0],self.read(i,op[1])[start:start+16])
        elif mnemonic in ['vpsrldq','vpslldq']:
            value=self.read(i,op[1]); count=min(op[2].imm,16)
            result=b''.join((value[start+count:start+16]+bytes(count)) if mnemonic=='vpsrldq'
                            else (bytes(count)+value[start:start+16-count]) for start in range(0,len(value),16))
            self.write(i,op[0],result)
        elif mnemonic == 'vpextrd':
            value=self.read(i,op[1]); start=(op[2].imm&3)*4
            self.write(i,op[0],value[start:start+4])
        elif mnemonic == 'bzhi':
            value = int.from_bytes(self.read(i, op[1], 4), 'little')
            index = int.from_bytes(self.read(i, op[2], 4), 'little') & 255
            result = value if index >= 32 else value & ((1 << index) - 1)
            flags = uc.reg_read(X.UC_X86_REG_EFLAGS) & ~(1 | 64 | 128 | 2048)
            flags |= int(index >= 32) | (64 if not result else 0) | (128 if result & 0x80000000 else 0)
            uc.reg_write(X.UC_X86_REG_EFLAGS, flags)
            self.write(i, op[0], result.to_bytes(4, 'little'))
        elif mnemonic in ['kmovq', 'kmovd', 'kmovw']:
            assert i.reg_name(op[0].reg) == 'k1'
            self.k[1] = int.from_bytes(self.read(i, op[1], {'kmovq':8,'kmovd':4,'kmovw':2}[mnemonic]), 'little')
        elif mnemonic == 'vpcmpleb':
            assert i.reg_name(op[0].reg) == 'k2'
            left = self.read(i, op[1]); right = self.read(i, op[2])
            signed = lambda b: b if b < 128 else b - 256
            self.k[2] = sum(int(signed(a) <= signed(b)) << j for j,(a,b) in enumerate(zip(left,right)))
        elif mnemonic == 'vpshufb':
            masked = len(op) == 4
            left = self.read(i, op[2] if masked else op[1])
            controls = self.read(i, op[3] if masked else op[2])
            value = bytes(0 if c & 128 else left[(j//16)*16+(c&15)] for j,c in enumerate(controls))
            if masked:
                assert i.reg_name(op[1].reg) == 'k2'
                previous = self.read(i, op[0])
                value = bytes(value[j] if self.k[2] >> j & 1 else previous[j] for j in range(len(value)))
            self.write(i, op[0], value)
        elif mnemonic == 'vinserti128':
            value = bytearray(self.read(i, op[1])); start = (op[3].imm & 1) * 16
            value[start:start+16] = self.read(i, op[2], 16)
            self.write(i, op[0], bytes(value))
        elif mnemonic == 'vmovdqu8':
            assert len(op) == 3 and i.reg_name(op[1].reg) == 'k1'
            mask = self.k[1]
            if op[0].type == capstone.x86_const.X86_OP_REG:
                assert '{z}' in i.op_str
                address = self.address(i, op[2]); value = bytearray(op[0].size)
                for j in range(len(value)):
                    if mask >> j & 1: value[j] = uc.mem_read(address + j, 1)[0]
                self.write(i, op[0], bytes(value))
            else:
                address = self.address(i, op[0]); value = self.read(i, op[2])
                for j in range(len(value)):
                    if mask >> j & 1: uc.mem_write(address + j, value[j:j+1])
        elif mnemonic in ['vmovdqu32', 'vmovdqu', 'vmovdqa', 'vmovups', 'vmovd']:
            n = 4 if mnemonic == 'vmovd' else op[0].size
            value = self.read(i, op[1], n)
            if op[0].size > n: value += bytes(op[0].size - n)
            self.write(i, op[0], value)
        elif mnemonic in ['vpbroadcastw','vpbroadcastd','vpbroadcastq','vbroadcasti128']:
            n = {'vpbroadcastw':2,'vpbroadcastd':4,'vpbroadcastq':8,'vbroadcasti128':16}[mnemonic]
            self.write(i, op[0], self.read(i, op[1], n) * (op[0].size // n))
        elif mnemonic == 'vextracti128' or mnemonic == 'vextracti32x8':
            n = op[0].size; start = (op[2].imm & 1) * n
            self.write(i, op[0], self.read(i, op[1])[start:start+n])
        elif mnemonic == 'vpshufd':
            value = self.read(i, op[1]); control = op[2].imm
            result = b''.join(value[start+4*((control >> (2*j)) & 3):start+4*((control >> (2*j)) & 3)+4]
                              for start in range(0, len(value), 16) for j in range(4))
            self.write(i, op[0], result)
        elif mnemonic == 'vpslld':
            value = self.read(i, op[1]); count = op[2].imm
            words = struct.unpack('<' + 'I' * (len(value)//4), value)
            self.write(i, op[0], b''.join(((v << count) & 0xffffffff).to_bytes(4, 'little') for v in words))
        else:
            left = self.read(i, op[1], op[0].size); right = self.read(i, op[2], op[0].size)
            n = op[0].size
            if mnemonic in ['vpxor','vpxord','vxorps']: result = bytes(a ^ b for a, b in zip(left, right))
            elif mnemonic == 'vpor': result = bytes(a | b for a,b in zip(left,right))
            elif mnemonic == 'vpaddb': result = bytes((a+b)&255 for a,b in zip(left,right))
            elif mnemonic == 'vpaddd':
                a = struct.unpack('<'+'I'*(n//4), left); b = struct.unpack('<'+'I'*(n//4), right)
                result = b''.join(((x+y)&0xffffffff).to_bytes(4, 'little') for x, y in zip(a, b))
            elif mnemonic == 'vpdpbusd':
                # Accumulate four unsigned-left/signed-right byte products
                # into each original destination dword, modulo 2**32.
                accumulator = struct.unpack('<'+'I'*(n//4), self.read(i, op[0]))
                signed = lambda b: b if b < 128 else b - 256
                result = b''.join(((accumulator[s//4] + sum(
                    left[j]*signed(right[j]) for j in range(s,s+4))) & 0xffffffff
                    ).to_bytes(4,'little') for s in range(0,n,4))
            elif mnemonic == 'vpsadbw':
                result = b''.join(sum(abs(left[j]-right[j]) for j in range(s, s+8)).to_bytes(8, 'little') for s in range(0, n, 8))
            elif mnemonic == 'vpmaddubsw':
                signed = lambda b: b if b < 128 else b - 256
                values = [max(-32768, min(32767, left[s]*signed(right[s])+left[s+1]*signed(right[s+1]))) for s in range(0, n, 2)]
                result = b''.join(v.to_bytes(2, 'little', signed=True) for v in values)
            elif mnemonic == 'vpmaddwd':
                a = struct.unpack('<'+'h'*(n//2), left); b = struct.unpack('<'+'h'*(n//2), right)
                result = b''.join(((a[s]*b[s]+a[s+1]*b[s+1])&0xffffffff).to_bytes(4, 'little') for s in range(0, n//2, 2))
            elif mnemonic == 'vpermd':
                indices = struct.unpack('<'+'I'*(n//4), left)
                result = b''.join(right[4*(index & (n//4-1)):4*(index & (n//4-1))+4] for index in indices)
            elif mnemonic == 'vpunpckhqdq':
                result = b''.join(left[s+8:s+16]+right[s+8:s+16] for s in range(0, n, 16))
            else: raise AssertionError((hex(pc), mnemonic, i.op_str))
            self.write(i, op[0], result)
        uc.reg_write(X.UC_X86_REG_EIP, pc + i.size)
