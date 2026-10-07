"""Initialize GUI alpha, clear and present the direct window without an off-screen FBO."""
import hashlib, json, pathlib
from console_fade_inputs import definition

R = pathlib.Path(__file__).resolve().parents[1]
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
BODY_SHA = {
    'loc_1004B302': 'dc13fa7f33667cdb40ae95809469140fcac339ac26a796ddeca5e29c6e15afd4',
    'loc_1004B317': 'a650f2398573c054c7e35e3fa9cb28eb9dbe1b321177edfb6526c763d3d3348d',
    'loc_1004210E': '99ece475ffd3516362e0b124444573fdc8602035f80944ef12ff713eabff817a',
    'loc_1004214F': 'd2970be6936196dbe486510e79c16e740a6d65d840ed299a938a9108a21fa47a',
    'loc_10043BBC': '11b0b800e97db91091c7d487f8f6e59fff262542d70a3bf34681092bcc13676d',
    'loc_10043C76': '95b9b13e7f3d6ed25d67396983d21f2228500e57487ccd0b5901d4d5a9191ddb',
    # These entry blocks bind framebuffer zero and reset the current-target
    # pointer. They do not clear its pixels or depth.
    'loc_100439C0': 'c1fee976d8b084c3aecdde88f8fbd77b6e1aa8db9c0ddd8812b2100833129ddf',
    'loc_10043A14': '8d9b5a2834856093f275e8366c2592ac046e74d9255cd0a066e8fdf07089b6cc',
    'loc_10043A2B': 'f28f1417b05f525b5aa11e251685f1c5fd10765c8b20a0afc62ac31095cc15f5',
}


def translate(text):
    assert sha(R / 'ref/qol/IFC22.dll') == 'eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607'
    for name, digest in BODY_SHA.items():
        assert hashlib.sha256(definition(text, name).encode()).hexdigest() == digest, name
    body = definition(text, 'loc_10043BBC')
    old = 'l_2:\n;tcall loc_10043BC9\nmusttail call fastcc void @loc_10043BC9(%_cpu* %cpu) nounwind\nret void'
    assert body.count(old) == 1
    # Preserve the original ce44 branch and its guest flags. A nonzero target
    # always follows the original resolve, including its checked object reads.
    # Only the same no-interop/default-FBO state used by window dimensions goes
    # directly to the original dirty-state update and normal function epilogue.
    new = 'l_2:\n'
    for name, address in [('target', 0x1046ce48), ('interop', 0x1046ce60),
                          ('current', 0x1046e258), ('framebuffer', 0x1046ce50)]:
        new += f'''%present_{name}_address = add i64 %2, {address}
%present_{name}_pointer = inttoptr i64 %present_{name}_address to i32*
%present_{name} = load i32, i32* %present_{name}_pointer, align 4
%present_{name}_zero = icmp eq i32 %present_{name}, 0
br i1 %present_{name}_zero, label %present_after_{name}, label %present_resolve
present_after_{name}:
'''
    new += '''musttail call fastcc void @loc_10043C76(%_cpu* %cpu) nounwind
ret void
present_resolve:
musttail call fastcc void @loc_10043BC9(%_cpu* %cpu) nounwind
ret void'''
    text = text.replace(body, body.replace(old, new), 1)
    body = definition(text, 'loc_1004210E')
    old = 'l_1:\nmusttail call fastcc void @loc_100421A2(%_cpu* %cpu) nounwind\nret void'
    assert body.count(old) == 1
    # The original frame-start clears the off-screen scene target at 4214F,
    # but skips it when that target is null. On the verified direct-window
    # route, use that SAME original GL clear and continuation for framebuffer
    # zero. Every nonzero target/mode/current binding keeps its original path.
    new = 'l_1:\n'
    for name, address in [('ui', 0x1046ce44), ('interop', 0x1046ce60),
                          ('current', 0x1046e258), ('framebuffer', 0x1046ce50)]:
        new += f'''%clear_{name}_address = add i64 %2, {address}
%clear_{name}_pointer = inttoptr i64 %clear_{name}_address to i32*
%clear_{name} = load i32, i32* %clear_{name}_pointer, align 4
%clear_{name}_zero = icmp eq i32 %clear_{name}, 0
br i1 %clear_{name}_zero, label %clear_after_{name}, label %clear_skip
clear_after_{name}:
'''
    new += '''musttail call fastcc void @loc_1004214F(%_cpu* %cpu) nounwind
ret void
clear_skip:
musttail call fastcc void @loc_100421A2(%_cpu* %cpu) nounwind
ret void'''
    text = text.replace(body, body.replace(old, new), 1)
    body = definition(text, 'loc_1004B302')
    old = 'l_1:\nmusttail call fastcc void @loc_1004B32D(%_cpu* %cpu) nounwind\nret void'
    assert body.count(old) == 1
    # Original GUI draws always use ONE / ONE_MINUS_SRC_ALPHA. With no
    # interop/FBO, initialization nevertheless skips PREMULTIPLY_ALPHA. Use
    # its existing formatter on the same bounded direct-window route. EAX
    # remains zero: the original emits "#define PREMULTIPLY_ALPHA 0", and
    # gui.frag deliberately uses #ifdef (definition, not numeric truth).
    # Preserve every original instruction, flag and nonzero renderer route.
    new = 'l_1:\n'
    for name, address in [('ui', 0x1046ce44), ('scene', 0x1046ce48),
                          ('current', 0x1046e258), ('framebuffer', 0x1046ce50)]:
        new += f'''%alpha_{name}_address = add i64 %35, {address}
%alpha_{name}_pointer = inttoptr i64 %alpha_{name}_address to i32*
%alpha_{name} = load i32, i32* %alpha_{name}_pointer, align 4
%alpha_{name}_zero = icmp eq i32 %alpha_{name}, 0
br i1 %alpha_{name}_zero, label %alpha_after_{name}, label %alpha_skip
alpha_after_{name}:
'''
    new += '''musttail call fastcc void @loc_1004B317(%_cpu* %cpu) nounwind
ret void
alpha_skip:
musttail call fastcc void @loc_1004B32D(%_cpu* %cpu) nounwind
ret void'''
    return text.replace(body, body.replace(old, new), 1)


def prepare(text):
    proof_path = R / 'generated/integration-review/apple-render-present/result.json'
    proof = json.loads(proof_path.read_text())
    assert proof['passed']
    for name, digest in proof['sources_sha256'].items():
        assert sha(R / name) == digest, name
    return translate(text), dict(original_blocks_sha256=BODY_SHA,
        script_sha256=sha(pathlib.Path(__file__)), test_sha256=sha(proof_path),
        scope='Original GUI premultiplication setup, frame clear and direct window completion only when target, interop, current target and framebuffer are zero. Original off-screen resolve and checked reads retained.')
