#!/usr/bin/env python3
"""Extract pinned default Wine collation tables and adapted comparison source."""
import argparse,ast,hashlib,json,pathlib,re,struct
R=pathlib.Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser();ap.add_argument('--check',action='store_true');a=ap.parse_args();ref=json.loads((R/'config/encoding-sources.json').read_text())['classification_reference'];raw=(R/ref['local_path']).read_bytes();assert hashlib.sha256(raw).hexdigest()==ref['sha256'];src=(R/ref['reader_local_path']).read_text();assert hashlib.sha256(src.encode()).hexdigest()==ref['reader_sha256']
h=struct.unpack_from('<4I',raw);o=h[3];version,guids=struct.unpack_from('<2I',raw,o);guid=struct.unpack_from('<5I',raw,o+24);assert guid[:4]==(0,0xffffffff,0,0),guid
p=o+8+36*guids;exp_count=struct.unpack_from('<I',raw,p)[0];exps=struct.unpack_from('<%dH'%(exp_count*2),raw,p+4);p+=4+exp_count*4;compr_count=struct.unpack_from('<I',raw,p)[0];p+=4;comp=struct.unpack_from('<I2H8H',raw,p+24*(compr_count-1));data=p+24*compr_count;t=data+comp[0]*2
for i in range(7):t+=comp[3+i]*((i+5)//2)*4
t+=4+struct.unpack_from('<I',raw,t)[0]//2*4
jamo_count=struct.unpack_from('<I',raw,t)[0];jamo=raw[t+4:t+4+8*jamo_count];assert len(jamo)==8*jamo_count and jamo_count>=0x165
keys=struct.unpack_from('<65536I',raw,h[0]);assert h[0]+65536*4<=h[1]
for v in keys:
 if (v>>8)&255==2:assert ((v>>16)&~0xc000)<exp_count
notice=src[:src.index('#include')]+ '\n/* Generated adaptation for the fixed default sort GUID.\n   Source/data SHA256: '+ref['reader_sha256']+' / '+ref['sha256']+'.\n   Unsupported locales are rejected outside this algorithm. */\n'
text=notice+'static const uint32_t nls_sort_keys[65536]={\n'+''.join(' '+','.join('0x%08x'%v for v in keys[i:i+8])+',\n' for i in range(0,len(keys),8))+'};\nstatic const struct sort_expansion nls_expansions[]={\n'+''.join(' {{0x%04x,0x%04x}},\n'%tuple(exps[i:i+2]) for i in range(0,len(exps),2))+'};\nstatic const struct jamo_sort nls_jamo[]={\n'+''.join(' {'+','.join(str(v) for v in jamo[i:i+5])+',{'+','.join(str(v) for v in jamo[i+5:i+8])+'}},\n' for i in range(0,len(jamo),8))+'};\n'
text+='static const struct sortguid default_sort={'+','.join('0x%x'%v for v in guid)+'};\nstatic const struct {const uint32_t *keys;const struct sort_expansion *expansions;const struct jamo_sort *jamo;} sort={nls_sort_keys,nls_expansions,nls_jamo};\n'
sections=[('static union char_weights get_char_weights(', 'static BYTE rol('),('struct sortkey\n','static const UINT *find_compression('),('/* get the zero digit','/* put one of the elements'),('struct sortkey_state\n','/* implementation of LCMAP_SORTKEY */'),('/* implementation of CompareStringEx */','/* implementation of FindNLSStringEx */')]
algorithm=''.join(src[src.index(start):src.index(end,src.index(start))] for start,end in sections)
algorithm=algorithm.replace('RtlAllocateHeap( GetProcessHeap(), 0, key->max )','malloc( key->max )').replace('else key->max = 0;','else collation_unsupported("native sort-key allocation failure");')
import re
algorithm=re.sub(r'RtlFreeHeap\( GetProcessHeap\(\), 0, ([^;]+) \);',r'free( \1 );',algorithm)
algorithm=algorithm.replace('ret += get_compression_weights( sortid->compr, compr_tables, src + pos, srclen - pos, &weights );','ret += default_compression_weights( sortid->compr, compr_tables );')
algorithm=algorithm.replace('except ? sort.keys[sort.keys[except + (c >> 8)] + (c & 0xff)] : sort.keys[c]','sort.keys[c]')
algorithm=algorithm.replace('    ret.val = sort.keys[c];','    if(except) collation_unsupported("unexpected default sort exception table");\n    ret.val = sort.keys[c];')
text+=algorithm
p=R/'port/core/nls_collation.inc'
if a.check:assert p.read_text()==text,'Run scripts/gen-nls-collation.py'
else:p.write_text(text)
print('PASS: pinned default sort weights for 65536 UTF-16 units, %d expansions, %d Jamo entries; NLS %x'%(exp_count,jamo_count,version))

reference=json.loads((R/'config/encoding-sources.json').read_text())['collation_reference']['supporting_sources']['tests'];test_source=(R/reference['local_path']).read_text();assert hashlib.sha256(test_source.encode()).hexdigest()==reference['sha256']
table=test_source[test_source.index('static const struct comparestringa_entry comparestringa_data[]'):test_source.index('static void test_CompareStringA')]
text='/* Adapted Wine 10.0 independent documented regression expectations.\n   LGPL-2.1-or-later; see vendor/wine-nls/LICENSE. ASCII values converted to UTF-16\n   for the declared English/default CompareStringEx profile; legacy A-only digit flag error excluded. */\n'
rows=[]
flag_values={'0':'0','SORT_STRINGSORT':'0x1000','NORM_IGNORESYMBOLS':'4','NORM_IGNORECASE':'1','LOCALE_USE_CP_ACP':'0x40000000','LINGUISTIC_IGNORECASE':'0x10','LINGUISTIC_IGNOREDIACRITIC':'0x20','NORM_IGNOREKANATYPE':'0x10000','NORM_IGNORENONSPACE':'2','NORM_IGNOREWIDTH':'0x20000','NORM_LINGUISTIC_CASING':'0x8000000'}
for line in table.splitlines():
 m=re.search(r'\{ LOCALE_SYSTEM_DEFAULT, (\w+|0), ("(?:\\.|[^"\\])*"), (-?\d+), ("(?:\\.|[^"\\])*"), (-?\d+), CSTR_(\w+) \}',line)
 if not m:continue
 flag,first,na,second,nb,result=m.groups();arrays=[]
 for k,value in enumerate((first,second)):
  value=ast.literal_eval(value);assert value.isascii();name='fixture_%d_%d'%(len(rows),k);arrays.append(name);text+='static const uint16_t '+name+'[]={'+','.join(str(ord(v)) for v in value)+',0,0,0};\n'
 rows.append('{%s,%s,%s,%s,%s,%d}'%(flag_values[flag],arrays[0],na,arrays[1],nb,{'LESS_THAN':1,'EQUAL':2,'GREATER_THAN':3}[result]))
assert len(rows)==50
text+='static const struct Case wine_cases[]={\n'+',\n'.join(rows)+'\n};\n';p=R/'tests/fixtures/collation_cases.inc'
if a.check:assert p.read_text()==text,'Run scripts/gen-nls-collation.py'
else:p.write_text(text)
print('PASS: 50 independent pinned Wine ASCII collation regression expectations converted to UTF-16')
