#!/usr/bin/env python3
"""Bots for Tribes: Vengeance: the community's TRIBESai mod (airfranse, 2006).

The base game has no multiplayer bots; its AI only runs the campaign. TRIBESai
adds them as an UnrealScript package (BaseBotsSTS.pkg, no Windows code) and four
multiplayer maps built for it (Fort, Emerald, Tropics, Feigned Beliefs), each with
a console in each base that switches its team's bots on. This fetches both
archives from the Tribes Revengeance community mirror, checks them, and unpacks
the nine files TribesPad uses into ref/vengeance/bots/Content (Classes, Maps,
HavokData), each checked too. Nothing of the mod goes into the repository; the
Mac app and the iOS app carry it (scripts/vengeance/bundle_mac.py,
scripts/build-ipad-engine.py) and the game finds it beside its own Content.

The archives use an early RAR method that only unar reads (brew install unar).
Without it the build goes on without bots.

  bots.py           fetch and unpack (quiet when already done)
  bots.py --check   exit 0 if the files are in place"""
import hashlib, pathlib, shutil, subprocess, sys, tempfile, urllib.request

R = pathlib.Path(__file__).resolve().parents[2]
OUT = R / 'ref/vengeance/bots'
MIRROR = 'https://downloads.tribesrevengeance.net'
ARCHIVES = {
    'TRIBESaiMod_V1_1.rar': ('/mods/TRIBESaiMod_V1_1.rar', '2e85246576d786482d1d482f7b6fc092fbb01f458683b9e1b4dee12e6ca75874'),
    'BotMaps.rar': ('/mappacks/BotMaps.rar', 'c0c715124a1bbc90e5ff10882c40285f63f2333c530916f991e297126714f298'),
}
FILES = {  # path in the archive (and under Content's parent here): archive, SHA-256
    'Content/Classes/BaseBotsSTS.pkg': ('TRIBESaiMod_V1_1.rar', '4f05bd616785d1c388a8893daf32d2997c078122791cc35ad6930590d51919df'),
    'Content/Maps/MP-EmeraldBots_V71.tvm': ('BotMaps.rar', '9a23d3ec17121f823807baafd060336df370486e28fb570084de4934a87bcba4'),
    'Content/Maps/MP-FeignedBeliefsBots_V1.tvm': ('BotMaps.rar', 'bd9dbe0750d41a5324d0f1ea810fb639aad23e319ab81d1700ef3397b4600d1e'),
    'Content/Maps/MP-FortBots_V5.tvm': ('BotMaps.rar', '9ce0e905e959e79ca4b7d8d907d48399cedbd150cbc97f511df7e29bdfccdef1'),
    'Content/Maps/MP-TropicsBots_V1.tvm': ('BotMaps.rar', 'a4a2342dfbc12f71f97a1677a06ffb1be936426ecc8835a885d23ae1107f3682'),
    'Content/HavokData/MP-EmeraldBots.mopp': ('BotMaps.rar', 'a435637e9022f1a080221fe6cbf3d9cbdf98ea8daff1a0cb4bb5f15668320af8'),
    'Content/HavokData/MP-FortBots.mopp': ('BotMaps.rar', '83bfdd59be0ec02527492eb814d0cd723a6c8adce19b66a8463a89ad8f3cabb8'),
    'Content/HavokData/MP-TropicsBots.mopp': ('BotMaps.rar', 'b342d1a325b08ebdff58c19a045b8c6037f2dc65a4d2bd686d8b05e7b2537b12'),
    'Content/HavokData/MP-VM-FeignedBeliefsBots.mopp': ('BotMaps.rar', 'a7116da68945cce4b492acdb6ddf332b3b427d7634616afb9261bb0d16e953b3'),
}
MAPS = ['MP-FortBots_V5', 'MP-EmeraldBots_V71', 'MP-TropicsBots_V1', 'MP-FeignedBeliefsBots_V1']
sha = lambda p: hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()

def ready():
    return all((OUT / f).exists() and sha(OUT / f) == digest for f, (_, digest) in FILES.items())

def fetch():
    if ready():
        return OUT
    unar = shutil.which('unar')
    if not unar:
        raise SystemExit('bots: unar is missing (brew install unar); building without Vengeance bots')
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        for name, (path, digest) in ARCHIVES.items():
            archive = tmp / name
            print('bots: downloading %s' % name, flush=True)
            request = urllib.request.Request(MIRROR + path, headers={'User-Agent': 'TribesPad'})
            with urllib.request.urlopen(request, timeout=120) as r, archive.open('wb') as f:
                shutil.copyfileobj(r, f)
            assert sha(archive) == digest, '%s: unexpected download (SHA-256 %s)' % (name, sha(archive))
            subprocess.run([unar, '-q', '-f', '-D', '-o', str(tmp / name.replace('.rar', '')), str(archive)], check=True)
        for f, (name, digest) in FILES.items():
            source = tmp / name.replace('.rar', '') / f
            assert source.exists() and sha(source) == digest, '%s: missing or changed in %s' % (f, name)
            (OUT / f).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, OUT / f)
    print('bots: %d files in %s' % (len(FILES), OUT), flush=True)
    return OUT

if __name__ == '__main__':
    if sys.argv[1:] == ['--check']:
        sys.exit(0 if ready() else 1)
    fetch()
