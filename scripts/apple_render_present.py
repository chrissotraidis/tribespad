"""Finish direct window rendering without resolving an absent off-screen FBO."""
import hashlib, json, pathlib
from console_fade_inputs import definition

R = pathlib.Path(__file__).resolve().parents[1]
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
BODY_SHA = {
    'loc_10043BBC': '11b0b800e97db91091c7d487f8f6e59fff262542d70a3bf34681092bcc13676d',
    'loc_10043C76': '95b9b13e7f3d6ed25d67396983d21f2228500e57487ccd0b5901d4d5a9191ddb',
    # These original entry blocks bind framebuffer zero and clear the current
    # target when the scene has no off-screen target. Keep that premise pinned.
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
    return text.replace(body, body.replace(old, new), 1)


def prepare(text):
    proof_path = R / 'generated/integration-review/apple-render-present/result.json'
    proof = json.loads(proof_path.read_text())
    assert proof['passed']
    for name, digest in proof['sources_sha256'].items():
        assert sha(R / name) == digest, name
    return translate(text), dict(original_blocks_sha256=BODY_SHA,
        script_sha256=sha(pathlib.Path(__file__)), test_sha256=sha(proof_path),
        scope='Direct window path only when target, interop, current target and framebuffer are zero. Original off-screen resolve and checked reads retained.')
