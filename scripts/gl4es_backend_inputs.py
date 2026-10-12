"""Pinned mobile desktop-GL implementation, with explicit guest bridge opt-in."""
import hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
def inputs(platform):
    assert platform in ('simulator','device','macos')
    root=R/'generated/gl4es'/platform
    report=json.loads((root/'result.json').read_text())
    assert report['platform']==platform
    assert subprocess.check_output(['git','-C',str(R/'ref/gl4es'),'rev-parse','HEAD'],text=True).strip()==report['commit']
    assert not subprocess.check_output(['git','-C',str(R/'ref/gl4es'),'status','--porcelain'],text=True).strip()
    library=root/'lib/libGL.a'
    assert hashlib.sha256(library.read_bytes()).hexdigest()==report['library_sha256']
    return report,['-DTB_GL4ES','-I',str(R/'ref/gl4es/include')],[str(library)]
