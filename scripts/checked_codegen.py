"""Generate optimized ARM64 objects from already checked LLVM input.

Keep the memory/external passes before code generation. llc uses its standard
backend preparation and machine optimizations, without a separate opt pipeline
or LTO that could see through the opaque, separately compiled guard runtime.
"""
import hashlib

FLAGS = ['-O2', '-filetype=obj', '-relocation-model=pic', '-verify-machineinstrs']

def identity(tools, target):
    compiler = tools / 'llc'
    return dict(compiler=str(compiler), sha256=hashlib.sha256(compiler.read_bytes()).hexdigest(),
                flags=FLAGS + ['-mtriple=' + target])

def command(tools, target, source, output):
    return [str(tools / 'llc'), *FLAGS, '-mtriple=' + target, str(source), '-o', str(output)]
