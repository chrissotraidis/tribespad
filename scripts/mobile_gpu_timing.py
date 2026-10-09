"""Disable IFC22's GPU-time estimator on mobile without pretending queries work.

The GLES backend has no verified GPU elapsed-time provider. Leave the original
rolling estimate and sample ring in their initialized, pre-first-sample state.
The frame scheduler therefore retains its original startup estimate. Only query
allocation, begin/end and ring/reporting are removed; rendering and GL errors
remain real. Desktop builds keep the original timing path.
"""
import hashlib
import pathlib
from console_fade_inputs import definition
R = pathlib.Path(__file__).resolve().parents[1]
IMAGE_SHA = 'eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607'
BLOCKS = {
    'loc_10047006': ('d347ba31d79c612212376d9d3822a85f320ea6a25402d7ee96d8670c26d840a7', 'loc_10047013'),
    'loc_10041CD0': ('eeb2922805a588d39906a7307cd8c5658649591c8d434e59bfd4a56723d7eac7', 'loc_10041CF6'),
    'loc_10041E86': ('9fdcdb8b0e92e514da25f525a36b9bc75f5e2d07a52cce684350b75fd146074f', 'loc_10041E91'),
    'loc_1004221C': ('6f80d88a246c922c44668f6483fa92a243ef6e36c2983709f40572e0ab8e4e1a', 'loc_100422F7'),
}

def translate(text):
    assert hashlib.sha256((R/'ref/qol/IFC22.dll').read_bytes()).hexdigest() == IMAGE_SHA
    for name, (digest, continuation) in BLOCKS.items():
        body = definition(text, name)
        assert hashlib.sha256(body.encode()).hexdigest() == digest, name
        if name == 'loc_10041CD0':
            # Keep the original register/flag updates and double-buffer toggle.
            marker = ';mov tmpadr, 0x1046d858\n'
            assert body.count(marker) == 1
            prefix = body.split(marker)[0]
        else:
            prefix = body.split('\n', 1)[0] + '\n'
        replacement = prefix + 'musttail call fastcc void @' + continuation + '(%_cpu* %cpu) nounwind\nret void\n}\n\n'
        text = text.replace(body, replacement, 1)
    return text
