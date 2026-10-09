"""Pinned ANGLE Metal frameworks for local Apple renderer builds."""
import hashlib
from pathlib import Path
import shutil
import urllib.request
import zipfile

R=Path(__file__).resolve().parents[1]
COMMIT='d2e5e488da0c6215f8276e5b8d992a4a6ec20c32'
URL='https://github.com/jeremyfa/build-angle/releases/download/angle-d2e5e48/angle-ios-universal.zip'
SHA256='1bbd5140d70de689f21f520581192a375d94d81d43844474c24623d545f6f2d2'
ROOT=R/'ref/angle-prebuilt'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def inputs(platform):
    assert platform in ('simulator','device')
    ROOT.mkdir(parents=True,exist_ok=True);archive=ROOT/'angle-ios-universal.zip'
    if not archive.exists():
        temporary=archive.with_suffix('.download');urllib.request.urlretrieve(URL,temporary)
        assert sha(temporary)==SHA256,'ANGLE download hash mismatch';temporary.replace(archive)
    assert sha(archive)==SHA256,'ANGLE archive hash mismatch'
    extracted=ROOT/'ios'
    # Check extracted bytes against the pinned archive, restoring changed inputs.
    with zipfile.ZipFile(archive) as z:
        for item in z.infolist():
            relative=Path(item.filename);assert not relative.is_absolute() and '..' not in relative.parts
            if item.is_dir():continue
            target=extracted/relative;data=z.read(item)
            if not target.exists() or target.read_bytes()!=data:
                target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
    assert (extracted/'commit.txt').read_text().strip()==COMMIT
    license=ROOT/'LICENSE.ANGLE'
    if not license.exists():urllib.request.urlretrieve('https://raw.githubusercontent.com/google/angle/'+COMMIT+'/LICENSE',license)
    slice='ios-arm64_x86_64-simulator' if platform=='simulator' else 'ios-arm64'
    frameworks=[extracted/(name+'.xcframework')/slice/(name+'.framework') for name in ('libEGL','libGLESv2')]
    report=dict(distributor='https://github.com/jeremyfa/build-angle',url=URL,archive_sha256=SHA256,reported_source_commit=COMMIT,platform=platform,frameworks={str(p.relative_to(R)):sha(p/p.stem) for p in frameworks})
    flags=['-DTB_ANGLE','-I',str(extracted/'include')]
    libraries=[x for p in frameworks for x in ['-F',str(p.parent),'-framework',p.stem]]+['-Wl,-rpath,@executable_path/Frameworks']
    return report,flags,libraries

def bundle(app,platform):
    report,_,_=inputs(platform)
    for name in report['frameworks']:
        source=R/name;target=app/'Frameworks'/source.name
        if target.exists():shutil.rmtree(target)
        shutil.copytree(source,target)
    shutil.copy2(ROOT/'LICENSE.ANGLE',app/'LICENSE.ANGLE')
    return [app/'Frameworks'/Path(name).name for name in report['frameworks']]
