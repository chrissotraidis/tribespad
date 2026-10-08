"""Resolve the implemented restart provider when the original GL3.1 group is skipped."""
import hashlib
import json
import pathlib
from console_fade_inputs import definition

R = pathlib.Path(__file__).resolve().parents[1]
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
BODY_SHA = {
    'loc_1001B445': '81f40482a1d860e5eb6cd6a1319da99dc6ef7993240ecfd9bbe51cac768eb12f',
    'loc_1001B463': '11d88fecafc5bacc2842a3a6142f024954ae2e4873d7fdfa269e4c96f60a23e4',
    'loc_1001B46A': '94a52f85826a4fede2d97766a0d2164b2b3c1a02a3587087b0fa0454e1d99791',
}


def translate(text):
    assert sha(R / 'ref/qol/IFC22.dll') == 'eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607'
    for name, digest in BODY_SHA.items():
        assert hashlib.sha256(definition(text, name).encode()).hexdigest() == digest, name
    body = definition(text, 'loc_1001B445')
    old = 'l_1:\nmusttail call fastcc void @loc_1001B4C9(%_cpu* %cpu) nounwind\nret void'
    assert body.count(old) == 1
    # The original consumer calls this slot even when GL3.1 is unavailable.
    # Resolve only its implemented provider, through the same finite registry
    # used by SDL_GL_GetProcAddress. Do not advertise GL3.1, run its remaining
    # resolver group, change guest registers/flags, or overwrite a loaded slot.
    new = '''l_1:
%restart_slot_address = add i64 %2, 273074952
%restart_slot_pointer = inttoptr i64 %restart_slot_address to i32*
%restart_slot = load i32, i32* %restart_slot_pointer, align 4
%restart_missing = icmp eq i32 %restart_slot, 0
br i1 %restart_missing, label %restart_resolve, label %restart_continue
restart_resolve:
%restart_provider = call i32 @tribes_graphics_get_proc(%_cpu* %cpu, i32 270259972)
%restart_available = icmp ne i32 %restart_provider, 0
br i1 %restart_available, label %restart_store, label %restart_continue
restart_store:
store i32 %restart_provider, i32* %restart_slot_pointer, align 4
br label %restart_continue
restart_continue:
musttail call fastcc void @loc_1001B4C9(%_cpu* %cpu) nounwind
ret void'''
    assert '@tribes_graphics_get_proc(' not in text
    return text.replace(body, body.replace(old, new), 1) + '\ndeclare i32 @tribes_graphics_get_proc(%_cpu*, i32)\n'


def prepare(text):
    proof_path = R / 'generated/integration-review/apple-primitive-restart/result.json'
    proof = json.loads(proof_path.read_text())
    assert proof['passed']
    for name, digest in proof['sources_sha256'].items():
        assert sha(R / name) == digest, name
    return translate(text), dict(original_blocks_sha256=BODY_SHA,
        script_sha256=sha(pathlib.Path(__file__)), test_sha256=sha(proof_path),
        scope='Resolve only the missing primitive-restart slot when the original GL3.1 resolver group is skipped. Actual finite provider result, original capability flag and continuation preserved.')
