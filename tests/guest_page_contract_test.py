"""Exercise actual original-x86 accesses, not merely page metadata."""
import pathlib
import struct
import sys
import unittest
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'scripts'))
import unicorn
from guest_page_contract import GuestPages, PageBoundary


class PageTests(unittest.TestCase):
    def setUp(self):
        self.u = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
        self.u.mem_map(0x1000, 0x5000)
        self.p = GuestPages(self.u)
        self.p.declare(0x1000, 0x1000, 0x20, 'code')
        self.p.declare(0x2000, 0x1000, 4, 'outputs')
        self.p.declare(0x3000, 0x2000, 4, 'data')
        self.p.declare(0x5000, 0x1000, 4, 'separate')

    def store(self, address):
        # mov byte ptr [absolute], 0x5a. API writes inject code before execution;
        # the guest store is checked by Unicorn's CPU, including page faults.
        self.u.mem_write(0x1000, b'\xc6\x05' + struct.pack('<I', address) + b'\x5a')
        self.u.emu_start(0x1000, 0x1007, count=1)

    def test_real_readonly_and_restore(self):
        self.u.mem_write(0x2000, b'\xa5' * 16)
        event = self.p.protect(0x3123, 128, 2, 0x2001)
        self.assertEqual(event['pages'], [0x3000])
        self.assertEqual(bytes(self.u.mem_read(0x2000, 6)), b'\xa5\x04\0\0\0\xa5')
        with self.assertRaises(unicorn.UcError): self.store(0x3123)
        self.store(0x4123)  # adjacent guest page retains write access
        self.p.protect(0x3000, 1, 4, 0x2001)
        self.assertEqual(struct.unpack('<I', self.u.mem_read(0x2001, 4))[0], 2)
        self.store(0x3123)
        self.assertEqual(bytes(self.u.mem_read(0x3123, 1)), b'Z')

    def test_straddling_first_page_old_value(self):
        self.p.protect(0x4000, 1, 2, 0x2000)
        event = self.p.protect(0x3fff, 2, 1, 0x2000)
        self.assertEqual(event['pages'], [0x3000, 0x4000])
        self.assertEqual(event['old_protection'], 4)
        for address in (0x3fff, 0x4000):
            with self.assertRaises(unicorn.UcError): self.store(address)

    def test_rejected_calls_do_not_mutate(self):
        before = dict(self.p.pages)
        for args in [(0x4fff, 2, 2, 0x2000), (0x3000, 0, 2, 0x2000),
                     (0xffffffff, 2, 2, 0x2000), (0x3000, 1, 2, 0),
                     (0x3000, 1, 2, 0x1000), (0x3000, 1, 2, 0x3000),
                     (0x3000, 1, 0x102, 0x2000), (0x6000, 1, 2, 0x2000)]:
            with self.assertRaises(PageBoundary): self.p.protect(*args)
            self.assertEqual(self.p.pages, before)
            self.assertEqual(self.p.changes, [])
        self.store(0x3000)

    def test_nonexecutable_fetch(self):
        self.u.mem_write(0x3000, b'\x90')
        with self.assertRaises(unicorn.UcError): self.u.emu_start(0x3000, 0x3001)


if __name__ == '__main__': unittest.main()
