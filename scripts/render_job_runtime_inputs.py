"""Adapt four pinned renderer lock calls to their cross-thread job-fence use."""
import hashlib,json,pathlib
import pefile
from renderer_batch_runtime_inputs import definition
R=pathlib.Path(__file__).resolve().parents[1]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
ROWS=[('loc_10043A71','loc_10075B40','loc_10043A7B',0x10043a76,'e8c5200300','6571f247e59dedc764c1187bb36192ba717e4285c8bd7e156e7b4ccf1ec69069'),
      ('loc_100439B0','loc_10075B50','loc_100439BA',0x100439b5,'e896210300','f69378fdb8f7e36b979881144dbbd8451444ba0e22d5736d9dc325068e59e8dc'),
      ('loc_10043AF3','loc_10075AE0','loc_10043AFD',0x10043af8,'e8e31f0300','2b8353b9650199f3f3b52566e6a156b416bd69b8c2edf6c5a8520d330c48c4a2'),
      ('loc_10043B81','loc_10075AF0','loc_10043B8B',0x10043b86,'e8651f0300','9baef21ea1cbe36ca174c1c36277f36fd279c59ee9bc4dd36efa3b1a85649335')]
def adapt(dll):
 image=R/'ref/qol/IFC22.dll'
 assert sha(image)=='eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607'
 pe=pefile.PE(str(image));records=[]
 for step,(name,target,continuation,pc,instruction,digest) in enumerate(ROWS):
  assert pe.get_data(pc-0x10000000,5).hex()==instruction
  body=definition(dll,name);assert hashlib.sha256(body.encode()).hexdigest()==digest,name
  old=f'musttail call fastcc void @{target}(%_cpu* %cpu) nounwind'
  assert body.count(old)==1
  new=body.replace(old,f'call void @tribes_render_job_step(%_cpu* %cpu, i32 {step})\nmusttail call fastcc void @{continuation}(%_cpu* %cpu) nounwind')
  assert dll.count(body)==1;dll=dll.replace(body,new,1)
  records.append(dict(name=name,pc=pc,step=step,before_sha256=digest,after_sha256=hashlib.sha256(new.encode()).hexdigest()))
 return dll+'\ndeclare void @tribes_render_job_step(%_cpu*, i32)\n',records
def prepare(dll,out):
 proof=json.loads((R/'generated/integration-review/render-job-handoff/result.json').read_text());assert proof['passed']
 for n,h in proof['sources_sha256'].items():assert sha(R/n)==h,n
 dll,records=adapt(dll);report=dict(records=records,test_sha256=sha(R/'generated/integration-review/render-job-handoff/result.json'),script_sha256=sha(pathlib.Path(__file__).resolve()))
 (out/'render-job-inputs.json').write_text(json.dumps(report,indent=2)+'\n');return dll,report
