#!/usr/bin/env python3
"""Game picker art, taken at build time from the player's own downloads.

Nothing from either game is in the repository. Tribes 2: the title screen of
its own installer (ref/downloads/tribes2gsi.exe, MAINDIR/Art/InstallScreen.
Background.bmp). Starsiege: Tribes: its title screen (BrightLogo in Shell.vol)
from the 2004 freeware installer, when ref/starsiege/tribes_gsi.exe exists
and has the pinned SHA-256 (docs/STARSIEGE-TRIBES-PLAN.md). Results are cached
in generated/launcher-art and refreshed when an installer changes.
"""
import hashlib, json, pathlib, struct, subprocess, sys, tempfile, zlib

R = pathlib.Path(__file__).resolve().parents[1]
CACHE = R / 'generated/launcher-art'
TRIBES2 = R / 'ref/downloads/tribes2gsi.exe'
STARSIEGE = R / 'ref/starsiege/tribes_gsi.exe'
STARSIEGE_SHA256 = 'd91ac445d30154aa45cd468e839bd540feca27ad62b3019a73e6fc4224fbb4a4'

def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()

def rewise():
    pin = {d['name']: d for d in json.loads((R / 'dependencies.lock.json').read_text())['sources']}['rewise']
    binary = R / 'ref/downloads' / ('rewise-' + pin['revision'][:12]) / 'build/rewise'
    return binary if binary.exists() else None

def extract(installer, member, folder):
    """Extract one installer with the pinned REWise; return the member path."""
    subprocess.run([str(rewise()), '-x', str(folder), str(installer)], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    path = folder / member
    return path if path.exists() else None

def png(path, width, height, rgb):
    rows = b''.join(b'\0' + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    chunk = lambda tag, data: struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))

def pvol(path):
    """Darkstar PVOL archive: name -> uncompressed bytes."""
    data = path.read_bytes()
    if data[:4] != b'PVOL':
        raise ValueError('not a PVOL archive')
    at, names, files = struct.unpack_from('<I', data, 4)[0], b'', {}
    while at + 8 <= len(data):
        tag, size = data[at:at + 4], struct.unpack_from('<I', data, at + 4)[0] & 0x7fffffff
        if tag == b'vols':
            names = data[at + 8:at + 8 + size]
        elif tag == b'voli':
            for k in range(size // 17):
                _, name, offset, length, packed = struct.unpack_from('<IIIIB', data, at + 8 + k * 17)
                if packed == 0 and data[offset:offset + 4] == b'VBLK':
                    files[names[name:names.index(b'\0', name)].decode('latin-1')] = data[offset + 8:offset + 8 + length]
        at += 8 + size
        if size == 0:
            break
    return files

def phoenix_bitmap(bitmap, palette_file, out):
    """8-bit Phoenix PBMP with a PL98 palette, written as PNG."""
    chunks, at = {}, 8
    while at + 8 <= len(bitmap):
        tag, size = bitmap[at:at + 4], struct.unpack_from('<I', bitmap, at + 4)[0]
        chunks[tag] = bitmap[at + 8:at + 8 + size]
        at += 8 + size
    _, width, height, depth, _ = struct.unpack_from('<IIIII', chunks[b'head'])
    if depth != 8 or palette_file[:4] != b'PL98':
        raise ValueError('unexpected bitmap or palette')
    palettes, at = {}, 52
    for _ in range(struct.unpack_from('<I', palette_file, 4)[0]):
        palettes[struct.unpack_from('<I', palette_file, at + 1024)[0]] = palette_file[at:at + 1024]
        at += 1032
    index = struct.unpack_from('<I', chunks[b'PiDX'])[0] if b'PiDX' in chunks else None
    colors = palettes.get(index) or next(iter(palettes.values()))
    png(out, width, height, bytes(c for v in chunks[b'data'] for c in colors[v * 4:v * 4 + 3]))

def cached(name, source):
    marker = CACHE / (name + '.json')
    image = CACHE / (name + '.png')
    digest = sha256(source)
    if image.exists() and marker.exists() and json.loads(marker.read_text()).get('source_sha256') == digest:
        return image, None
    return image, lambda: marker.write_text(json.dumps({'source': str(source.relative_to(R)), 'source_sha256': digest}) + '\n')

def tribes2():
    if not TRIBES2.exists() or not rewise():
        return None
    image, done = cached('tribes2', TRIBES2)
    if done:
        with tempfile.TemporaryDirectory() as folder:
            bmp = extract(TRIBES2, 'MAINDIR/Art/InstallScreen.Background.bmp', pathlib.Path(folder))
            if not bmp:
                return None
            subprocess.run(['sips', '-s', 'format', 'png', str(bmp), '--out', str(image)], check=True, stdout=subprocess.DEVNULL)
        done()
    return image

def starsiege():
    if not STARSIEGE.exists() or not rewise() or sha256(STARSIEGE) != STARSIEGE_SHA256:
        return None
    image, done = cached('starsiege', STARSIEGE)
    if done:
        with tempfile.TemporaryDirectory() as folder:
            shell = extract(STARSIEGE, 'MAINDIR/Install/Tribes/base/Shell.vol', pathlib.Path(folder))
            if not shell:
                return None
            files = pvol(shell)
            phoenix_bitmap(files['BrightLogo.bmp'], files['Shell.ppl'], image)
        done()
    return image

def prepare(out):
    """Copy the art that exists into out (tribes2.png, starsiege.png)."""
    CACHE.mkdir(parents=True, exist_ok=True)
    found = {}
    for name, make in (('tribes2', tribes2), ('starsiege', starsiege)):
        try:
            image = make()
        except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
            print('launcher art: no %s art (%s)' % (name, error), file=sys.stderr)
            image = None
        if image:
            (out / (name + '.png')).write_bytes(image.read_bytes())
            found[name] = str(image.relative_to(R))
    return found

if __name__ == '__main__':
    target = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else CACHE
    target.mkdir(parents=True, exist_ok=True)
    print(json.dumps(prepare(target), indent=1))
