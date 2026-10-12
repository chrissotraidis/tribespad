# Loaded by SCons through SCONSFLAGS=--site-dir=... (scripts/build-tools.py).
# SCons starts compilers with a minimal environment; keep the SDK that
# build-tools.py selected so a mismatched Command Line Tools SDK is not used.
import os
import SCons.Environment

_init = SCons.Environment.Base.__init__

def _init_with_sdk(self, *args, **kwargs):
    _init(self, *args, **kwargs)
    for key in ('SDKROOT', 'DEVELOPER_DIR'):
        if key in os.environ:
            self['ENV'][key] = os.environ[key]

SCons.Environment.Base.__init__ = _init_with_sdk

