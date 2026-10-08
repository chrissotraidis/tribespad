#!/usr/bin/env python3
"""Prepare a separate offline-host profile that can discover internet servers.

Use the original browser/master-query implementation. This neither changes
authentication state nor opens a gameplay connection.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

ROOT=Path(__file__).resolve().parents[1]
source=ROOT/'generated/integration-review/offline-menu/game-profile'
out=ROOT/'generated/integration-review/mac-server-browser'
profile=out/'game-profile'
out.mkdir(parents=True,exist_ok=True)
if not profile.exists():
    subprocess.run(['cp','-cR',str(source),str(profile)],check=True)
archive=profile/'base/scripts.vl2'
sha=lambda data:hashlib.sha256(data).hexdigest()
assert sha(archive.read_bytes())=='610463dbac0cd5e48f7cff775a6f04b23d2e35adc2779e8a00c127b86c530490'
with zipfile.ZipFile(archive) as z:
    original=z.read('scripts/GameGui.cs')
changes=[
    (b'if ( isDemo() || $PlayingOnline )',b'if ( isDemo() || $PlayingOnline || $pref::TribesPad::InternetBrowser )'),
    (b'GMJ_Browser.lastQuery = $PlayingOnline ? "Master" : "LanServers";',
     b'GMJ_Browser.lastQuery = ($PlayingOnline || $pref::TribesPad::InternetBrowser) ? "Master" : "LanServers";'),
]
patched=original
for before,after in changes:
    assert patched.count(before)==1
    patched=patched.replace(before,after)
script=profile/'base/scripts/GameGui.cs'
script.parent.mkdir(parents=True,exist_ok=True)
if script.exists():
    assert script.read_bytes() in (original,patched)
script.write_bytes(patched)
# Only this isolated profile's stale compiled GUI cache is invalidated.
cache=script.with_suffix('.cs.dso')
if cache.exists():
    cache.unlink()
autoexec=profile/'base/autoexec.cs'
original_autoexec=autoexec.read_bytes()
setting=b'\n// Internet discovery only; original offline hosting and authentication state remain.\n$pref::TribesPad::InternetBrowser = true;\n'
if setting not in original_autoexec:
    autoexec.write_bytes(original_autoexec+setting)
receipt=dict(profile=str(profile),source_profile=str(source),
    archive_sha256=sha(archive.read_bytes()),original_script_sha256=sha(original),
    script_sha256=sha(patched),autoexec_sha256=sha(autoexec.read_bytes()),
    replacements=len(changes),original_engine_executed=False,
    scope='Original internet master/server queries enabled in an isolated offline profile; no authentication or game join.')
(out/'profile.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(profile)
