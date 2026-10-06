"""Real Unicorn cache invalidation for original-x86 diagnostics only.
This neither publishes ARM64 code nor makes guest patch bytes affect AOT calls.
"""
class CacheBoundary(ValueError):
    pass

class ReferenceInstructionCache:
    def __init__(self, emulator, pages):
        self.emulator=emulator;self.pages=pages;self.events=[];self.running=False

    def validate(self, address, size):
        if not 0<=address<=0xffffffff or not 0<=size<=0xffffffff:
            raise CacheBoundary('not an x86 DWORD range')
        if address:
            if not size or address+size>0x100000000:
                raise CacheBoundary('empty or overflowing diagnostic cache range')
            self.pages.require(address,size,0)

    def invalidate(self, address, size):
        if self.running:raise CacheBoundary('pause emulation before invalidating translations')
        self.validate(address,size)
        if address:
            self.emulator.ctl_remove_cache(address,address+size);method='ctl_remove_cache'
        else:
            self.emulator.ctl_flush_tb();method='ctl_flush_tb'
        event={'address':address,'bytes':size,'method':method,'completed':True,'reference_only':True,'native_arm64_publication':False}
        self.events.append(event);return event
