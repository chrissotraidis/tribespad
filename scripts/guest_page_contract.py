"""Enforced 4 KiB x86 page contract for original-code diagnostics in Unicorn.

This is not a Darwin VirtualProtect implementation. Unsupported/invalid calls
raise a diagnostic boundary instead of inventing Win32 error behavior.
"""
import struct
import unicorn

PAGE_SIZE = 4096
PERMISSIONS = {
    1: unicorn.UC_PROT_NONE,
    2: unicorn.UC_PROT_READ,
    4: unicorn.UC_PROT_READ | unicorn.UC_PROT_WRITE,
    0x10: unicorn.UC_PROT_EXEC,
    0x20: unicorn.UC_PROT_READ | unicorn.UC_PROT_EXEC,
    0x40: unicorn.UC_PROT_ALL,
}


class PageBoundary(ValueError):
    pass


class GuestPages:
    def __init__(self, emulator):
        self.emulator = emulator
        self.pages = {}
        self.changes = []

    @staticmethod
    def span(address, size):
        if address < 0 or size <= 0 or address + size > 0x100000000:
            raise PageBoundary('invalid DWORD page range')
        return range(address & ~4095, (address + size + 4095) & ~4095, 4096)

    def declare(self, address, size, protection, reservation):
        if address % 4096 or size % 4096 or protection not in PERMISSIONS:
            raise PageBoundary('invalid committed-page declaration')
        pages = self.span(address, size)
        if any(page in self.pages for page in pages):
            raise PageBoundary('overlapping committed-page declaration')
        self.emulator.mem_protect(address, size, PERMISSIONS[protection])
        for page in pages:
            self.pages[page] = (reservation, protection)

    def require(self, address, size, permission):
        for page in self.span(address, size):
            state = self.pages.get(page)
            if state is None or PERMISSIONS[state[1]] & permission != permission:
                raise PageBoundary('undeclared or inaccessible page at 0x%08x' % page)

    def protect(self, address, size, protection, old_output):
        if protection not in PERMISSIONS:
            raise PageBoundary('unsupported protection/modifier 0x%x' % protection)
        pages = list(self.span(address, size))
        states = [self.pages.get(page) for page in pages]
        if any(state is None for state in states):
            raise PageBoundary('range includes an undeclared/uncommitted page')
        if len({state[0] for state in states}) != 1:
            raise PageBoundary('range crosses distinct reservations')
        if not old_output:
            raise PageBoundary('NULL old-protection output')
        self.require(old_output, 4, unicorn.UC_PROT_WRITE)
        if not PERMISSIONS[protection] & unicorn.UC_PROT_WRITE:
            if any(page in pages for page in self.span(old_output, 4)):
                raise PageBoundary('output overlaps newly nonwritable pages')
        changed = []
        try:
            for page in pages:
                self.emulator.mem_protect(page, 4096, PERMISSIONS[protection])
                changed.append(page)
        except unicorn.UcError:
            for page in changed:
                self.emulator.mem_protect(page, 4096, PERMISSIONS[self.pages[page][1]])
            raise
        for page, state in zip(pages, states):
            self.pages[page] = (state[0], protection)
        self.emulator.mem_write(old_output, struct.pack('<I', states[0][1]))
        event = {'address': address, 'size': size, 'new_protection': protection,
                 'old_protection': states[0][1], 'old_output': old_output,
                 'pages': pages, 'reservation': states[0][0],
                 'enforcement': 'Unicorn guest 4 KiB memory permissions'}
        self.changes.append(event)
        return event
