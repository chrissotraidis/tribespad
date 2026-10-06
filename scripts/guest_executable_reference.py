"""Original-x86 generated-code capture in Unicorn, never Apple executable memory.

The native allocator remains fail-closed. This reference backend allocates real
Unicorn pages only after both native logical ownership and Unicorn mappings agree
that the requested range is free. It preserves allocation identity across guest
permission changes and clips otherwise-native free queries at reference owners.
"""
import struct
import unicorn
from guest_page_contract import PERMISSIONS, PageBoundary

class ExecutableReference:
    def __init__(self, emulator, pages, native_query):
        self.emulator = emulator
        self.pages = pages
        self.native_query = native_query
        self.allocations = {}
        self.events = []

    def containing(self, address, size=1):
        for start, record in self.allocations.items():
            if start <= address and address + size <= start + record['bytes']:
                return start, record
        return None

    def query(self, address):
        page = address & ~4095
        owned = self.containing(page)
        if owned:
            start, record = owned
            protection = self.pages.pages[page][1]
            end = page + 4096
            while end < start + record['bytes'] and self.pages.pages[end][1] == protection:
                end += 4096
            return [page, start, record['initial_protect'], end-page, 0x1000, protection, 0x20000], True
        fields = list(self.native_query(address))
        clipped = False
        if fields[4] == 0x10000:
            for start in self.allocations:
                if page < start < page + fields[3]:
                    fields[3] = start-page
                    clipped = True
        return fields, clipped

    def allocate(self, address, size, kind, protection):
        if (not 0x10000 <= address < 0x30000000 or address % 65536 or
                not size or size % 65536 or size > 0x400000 or
                address+size > 0x30000000 or kind != 0x3000 or protection != 0x40):
            raise PageBoundary('executable capture supports bounded fixed 64 KiB aligned reserve+commit RWX only')
        cursor = address
        evidence = []
        while cursor < address+size:
            fields, _ = self.query(cursor)
            if fields[4] != 0x10000 or fields[3] <= 0:
                raise PageBoundary('executable capture overlaps owned guest memory')
            evidence.append(fields)
            cursor += min(fields[3], address+size-cursor)
        if any(page in self.pages.pages for page in range(address,address+size,4096)):
            raise PageBoundary('executable capture overlaps declared Unicorn pages')
        self.emulator.mem_map(address,size,unicorn.UC_PROT_ALL)
        self.pages.declare(address,size,protection,('executable_reference',address))
        self.allocations[address]={'bytes':size,'initial_protect':protection}
        self.events.append({'kind':'allocate','address':address,'bytes':size,'protection':protection,
                            'native_free_queries':evidence,'backend':'real Unicorn x86 pages; no native ARM64 execution/publication'})
        return address

    def protect(self, address, size, protection, old_output):
        low=address&~4095;high=(address+size+4095)&~4095
        if not self.containing(low,high-low):
            raise PageBoundary('executable reference protection crosses allocation ownership')
        event=self.pages.protect(address,size,protection,old_output)
        self.events.append({'kind':'protect',**event})
        return event

    def release(self, address, size, kind):
        if kind != 0x8000 or size or address not in self.allocations:
            raise PageBoundary('executable capture release requires exact allocation base and zero size')
        record=self.allocations[address]
        self.emulator.mem_unmap(address,record['bytes'])
        for page in range(address,address+record['bytes'],4096):
            del self.pages.pages[page]
        del self.allocations[address]
        self.events.append({'kind':'release','address':address,'bytes':record['bytes']})
        return 1
