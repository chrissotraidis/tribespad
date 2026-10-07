"""Finite, frame-local FNSTENV/FLDENV query pair with dead stores removed.

This does not provide standalone environment instructions. The original
function never exposes the environment address, calls another function, or
reads the saved pointers/status/tags after restoring them. Only masked CW is
live. Keep the full memory-access guard despite eliminating dead stack stores.
"""
import hashlib, pathlib, re
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_GRP_JUMP, CS_GRP_CALL
from capstone.x86 import X86_OP_MEM, X86_OP_IMM, X86_REG_EBP
R = pathlib.Path(__file__).resolve().parents[1]
START, END = 0x1011d750, 0x1011d927
PAIR, RESTORE, MXCSR = 0x1011d76e, 0x1011d771, 0x1011d836
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()

def verify(image):
    assert sha(image) == 'eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607'
    pe = pefile.PE(str(image))
    decoder = Cs(CS_ARCH_X86, CS_MODE_32); decoder.detail = True
    instructions = list(decoder.disasm(pe.get_data(START-0x10000000, END-START), START))
    assert instructions[-1].address == END-1 and instructions[-1].mnemonic == 'ret'
    assert sum(i.size for i in instructions) == END-START
    accesses = []
    for ins in instructions:
        assert not ins.group(CS_GRP_CALL) and ins.mnemonic != 'lea'
        if ins.group(CS_GRP_JUMP):
            assert ins.operands[0].type == X86_OP_IMM
            assert START <= ins.operands[0].imm < END
            assert ins.operands[0].imm not in (PAIR, RESTORE, PAIR+1, PAIR+2, RESTORE+1, RESTORE+2)
        for op in ins.operands:
            if op.type != X86_OP_MEM: continue
            if op.mem.base == X86_REG_EBP:
                assert not op.mem.index
                accesses.append((ins.address, ins.mnemonic, op.mem.disp, op.size))
            else:
                assert ins.address == 0x1011d829 and not op.mem.base and not op.mem.index and op.mem.disp == 0x1046bfd0
    assert accesses == [
        (0x1011d75c,'mov',-8,4),(0x1011d765,'movups',-32,16),
        (0x1011d769,'movq',-16,8),(PAIR,'fnstenv',-32,28),
        (RESTORE,'fldenv',-32,28),(0x1011d774,'mov',-32,4),
        (0x1011d79c,'mov',-4,4),(0x1011d7a5,'mov',-4,4),
        (0x1011d7ae,'mov',-4,4),(0x1011d824,'or',-4,4),
        (MXCSR,'stmxcsr',-4,4),(0x1011d83a,'mov',-4,4)]
    assert pe.get_data(PAIR-0x10000000,15).hex() == 'd975e0d965e08b55e081e23f1f0000'
    assert pe.get_data(MXCSR-0x10000000,4).hex() == '0fae5dfc'
    return dict(image_sha256=sha(image),start=START,end=END,frame_accesses=accesses,
                eliminated_dead_environment_bytes=26,guarded_environment_bytes=28)

def translate(text, image=R/'ref/qol/IFC22.dll'):
    proof = verify(image)
    first = 'call void @halopad_trap_unimplemented(i32 %d) nounwind' % PAIR
    second = 'call void @halopad_trap_unimplemented(i32 %d) nounwind' % RESTORE
    save = 'call void @halopad_trap_unimplemented(i32 %d) nounwind' % MXCSR
    assert all(text.count(x) == 1 for x in [first,second,save])
    between = text[text.index(first)+len(first):text.index(second)]
    assert all(not line.strip() or line.lstrip().startswith(';') for line in between.splitlines())
    assert '@tb_fp_control_query_' not in text
    text = text.replace(first,'call void @tb_fp_control_query_pair(%_cpu* %cpu)',1)
    text = text.replace(second,'; FLDENV is fused with its adjacent private-frame save.',1)
    text = text.replace(save,'call void @tb_fp_control_query_mxcsr(%_cpu* %cpu)',1)
    text += '\ndeclare void @tb_fp_control_query_pair(%_cpu*)\ndeclare void @tb_fp_control_query_mxcsr(%_cpu*)\n'
    return text, proof
