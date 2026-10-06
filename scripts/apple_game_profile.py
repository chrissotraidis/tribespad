"""Create an isolated Apple profile with bundled fonts and no optional Discord SDK.
The reference installation remains unchanged. No missing-provider fiction:
the excluded library is physically absent from the mounted runtime profile.
"""
import hashlib,json,pathlib,subprocess
import pefile,struct
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def prepare():
 source=R/'ref/product';out=R/'generated/apple-game-profile';out.mkdir(exist_ok=True)
 excluded='discord_game_sdk.dll';sdk=source/excluded;script=source/'console_client_discord.cs';dll=source/'IFC22.dll'
 assert sha(sdk)=='d8e7d9feb3de8482b186ae44fd1c9abb41fe2b3b3d2c7cd3a4d742ebbad30cdf'
 assert sha(script)=='cb06fe9b35318d845a360990d1876b96cc3c59a2e58790b8d15a1f8df29d2c41'
 assert sha(dll)=='eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607'
 # Actual original post-LoadLibrary path: store handle, test it, skip the
 # optional DiscordCreate/setup region when no SDK is installed.
 pe=pefile.PE(str(dll));code=pe.get_data(0x60d04,0x31);assert code[:13].hex()=='a390c6461085c00f8424010000'
 # Verify this fixed three-instruction branch without allocating executable
 # emulator memory during native build preparation. With EAX=0, MOV stores
 # zero, TEST sets ZF, and the pinned JE reaches the original continuation.
 assert struct.unpack_from('<I',code,1)[0]==0x1046c690
 assert 0x10060d04+13+struct.unpack_from('<i',code,9)[0]==0x10060e35
 # IFC22 explicitly checks $Font::SubstituteArial before loading a Windows
 # fallback face. Choose the Univers SDF shipped in t2csri.vl2 for this profile.
 font_settings=b'// Apple profile: use the bundled SDF face for the Arial fallback.\n$Font::SubstituteArial = "Univers";\n'
 startup_settings=font_settings+b'$pref::SkipIntro = true; // Original preference: proceed directly to login.\n'
 files=[];expected=set();overrides=[]
 for p in sorted(source.rglob('*')):
  assert not p.is_symlink(),p
  rel=p.relative_to(source)
  if p.is_dir():(out/rel).mkdir(exist_ok=True);continue
  assert p.is_file(),p
  if str(rel)==excluded:continue
  target=out/rel;expected.add(str(rel));digest=sha(p)
  if not target.exists():subprocess.run(['cp','-c',str(p),str(target)],check=True)
  if str(rel) in ('console_start.cs','base/prefs/ClientPrefs.cs'):
   original=p.read_bytes()
   if str(rel)=='console_start.cs':
    desired=startup_settings+original
    previous_digest=hashlib.sha256(font_settings+original).hexdigest()
    reason='Original preferences select bundled Univers for Arial and skip the optional AVI intro; original login remains enabled.'
   else:
    desired=original+b'\n// Apple profile: native VSync pacing; optional software limiter disabled.\n$pref::Engine::FramerateLimit = 0;\n$pref::Video::disableVerticalSync = false;\n'
    previous_digest=digest
    reason='Original client settings use native VSync with the optional SDL timer frame limiter disabled.'
   desired_digest=hashlib.sha256(desired).hexdigest()
   assert target.is_file() and not target.is_symlink() and sha(target) in (digest,previous_digest,desired_digest),(p,target)
   if sha(target)!=desired_digest:target.write_bytes(desired)
   overrides.append(dict(path=str(rel),source_sha256=digest,sha256=desired_digest,reason=reason))
   digest=desired_digest
  assert target.is_file() and not target.is_symlink() and sha(target)==digest,(p,target)
  files.append(dict(path=str(rel),bytes=target.stat().st_size,sha256=digest))
 actual={str(p.relative_to(out)) for p in out.rglob('*') if p.is_file()}
 runtime_cache=sorted(actual-expected)
 # Original script compilation writes DSO caches into this writable clone.
 # Preserve them across builds; they are outputs, never reference inputs.
 assert expected<=actual
 for name in runtime_cache:
  target=out/name
  assert name.startswith('base/') and name.lower().endswith('.dso') and not target.is_symlink(),name
  for parent in target.parents:
   assert not parent.is_symlink(),name
   if parent==out:break
 report=dict(runtime_cache=runtime_cache,profile='apple-bundled-fonts-no-discord-sdk',source=str(source.relative_to(R)),root=str(out.relative_to(R)),overrides=overrides,excluded=[dict(path=excluded,source_sha256=sha(sdk),reason='Optional Discord presence/invite SDK is not included in this Apple runtime profile.')],files=files,original_absence_branch=dict(module_sha256=sha(dll),instructions_hex=code[:13].hex(),verification='exact original bytes and decoded branch target; static zero-handle semantics',entry=0x10060d04,continuation=0x10060e35,script_sha256=sha(script)),generator_sha256=sha(pathlib.Path(__file__).resolve()),scope='Copy-on-write reference projection excluding the optional Discord Windows SDK and selecting the bundled Univers Arial fallback and skipping the optional AVI intro and using VSync instead of the optional software frame limiter with original client settings. Original reference preserved; no native provider, fake handle, success result, or core authentication/gameplay code replacement.')
 manifest=R/'generated/apple-game-profile.json';manifest.write_text(json.dumps(report,indent=2)+'\n');return out,dict(manifest=str(manifest.relative_to(R)),sha256=sha(manifest),profile=report['profile'],excluded=report['excluded'])
if __name__=='__main__':
 out,report=prepare();print(json.dumps(report,indent=2))
