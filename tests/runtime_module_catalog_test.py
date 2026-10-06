"""Reject absent-module declarations contradicted by real configured inputs."""
import hashlib,pathlib,sys,tempfile
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'scripts'))
from runtime_module_catalog import audit_absent_module
with tempfile.TemporaryDirectory() as temp:
 root=pathlib.Path(temp);game=root/'Game';volume=root/'Volume';game.mkdir();volume.mkdir();(game/'other.dll').write_bytes(b'actual inventory fixture')
 result=audit_absent_module('dbghelp.dll',[game,volume],['C:\\Windows\\System32\\kernel32.dll']);assert result['absent'] and result['roots'][0]['dlls'][0]['sha256']==hashlib.sha256(b'actual inventory fixture').hexdigest()
 def rejected(providers):
  try:audit_absent_module('dbghelp.dll',[game,volume],providers)
  except AssertionError:return
  raise AssertionError('contradictory absence accepted')
 rejected(['C:\\Windows\\System32\\DBGHELP.DLL']);(game/'DBGHELP.dLl').write_bytes(b'present');rejected([]);(game/'DBGHELP.dLl').unlink();(volume/'dbghelp.dll').mkdir();rejected([]);(volume/'dbghelp.dll').rmdir();(game/'nested').mkdir();(game/'nested/dbghelp.dll').write_bytes(b'present');rejected([]);(game/'nested/dbghelp.dll').unlink();(game/'alias.dll').symlink_to(game/'other.dll');rejected([])
print('PASS: independent absence audit, actual DLL content hashes and rejection of provider/file/directory/nested/case/symlink contradictions')
