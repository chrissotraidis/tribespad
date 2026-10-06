#!/usr/bin/env python3
"""Evaluate Intel's numeric accuracy limits; does not certify exception flags."""
import hashlib,json,pathlib
import mpmath as mp
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/bochs-math/tests'
trace=json.loads((out/'trace.json').read_text())
def value(raw):
 m,e=raw;exponent=e&0x7fff
 if exponent==0x7fff:return mp.nan
 z=mp.mpf(m)*mp.power(2,(exponent or 1)-16383-63)
 return -z if e&0x8000 else z
def evaluate(dps):
 mp.mp.dps=dps;families={str(op):{'cases':0,'max_ulp':mp.mpf(0),'failures':0,'excluded':0} for op in range(6)};failures=[]
 for row in trace:
  op=row['op']
  if op>=6:continue
  family=families[str(op)];a=value(row['a']);b=value(row['b']);actual=value(row['result']);rc=(row['cw']>>10)&3
  # Intel 8.3.10 restricts trig error guarantees to reduced arguments.
  if mp.isnan(actual) or (op==1 and abs(a)>=3*mp.pi/4) or (op in [2,3] and abs(a)>=3*mp.pi/8) or (op==4 and a<=0) or (op==5 and a==0 and b==0):family['excluded']+=1;continue
  ideal=[lambda:mp.expm1(a*mp.log(2)),lambda:mp.sin(a),lambda:mp.cos(a),lambda:mp.tan(a),lambda:b*mp.log(a,2),lambda:mp.atan2(b,a)][op]()
  # mpmath has no signed zero; x87 atan2(-0, negative) is -pi, not +pi.
  if op==5 and b==0 and a<0:ideal=-mp.pi if row['b'][1]&0x8000 else mp.pi
  if ideal==0:error=mp.mpf(0) if actual==0 else mp.inf
  else:
   ulp=mp.power(2,int(mp.floor(mp.log(abs(ideal),2)))-63)
   error=abs(actual-ideal)/ulp
  bound=mp.mpf('1.35') if rc==0 and op==4 and b!=1 else mp.mpf(1 if rc==0 else '1.5')
  family['cases']+=1;family['max_ulp']=max(family['max_ulp'],error)
  if error>=bound:
   family['failures']+=1
   if len(failures)<12:failures.append({'op':op,'cw':hex(row['cw']),'a':row['a'],'b':row['b'],'result':row['result'],'error_ulp':str(error),'bound':str(bound)})
 for f in families.values():f['max_ulp']=mp.nstr(f['max_ulp'],30)
 return {'families':families,'failures':failures}
low=evaluate(100);high=evaluate(160);assert low['families']==high['families'],'Reference precision was insufficient'
report={'sampled_numeric_bounds_pass':not high['failures'],'reference':'mpmath 1.3.0, 100 and 160 decimal digits','architecture_reference':'Pinned Intel SDM 089, PDF pages 228–230, Vol 1 sections 8.3.8–8.3.10','trace_sha256':hashlib.sha256((out/'trace.json').read_bytes()).hexdigest(),**high,'exception_status_verified':False,'whole_engine_acceptance':False,'limits':['A sampled census cannot establish monotonicity or a global accuracy guarantee','Out-of-reduction trig arguments and exceptional inputs are excluded from numeric limits, not accepted as passing','C1 and PE differences from Rosetta require further instruction-status investigation']}
(out/'accuracy.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
