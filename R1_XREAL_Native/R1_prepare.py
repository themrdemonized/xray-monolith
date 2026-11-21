from pathlib import Path
import json, shutil, sys
R=Path(__file__).parent
reference=Path('C:/0_CHATGPT_ANOMALY/work/r1_echo_mt_20260912')
sys.path.insert(0,str(reference/'toolkit'))
from inspect_host import validate,pe_info
(R/'evidence').mkdir(exist_ok=True)
(R/'package/gamedata/plugins/R1_XREAL_Native').mkdir(parents=True,exist_ok=True)
if not (R/'vendor/minhook').exists():
    shutil.copytree(reference/'vendor/minhook',R/'vendor/minhook')
rows=[]
for host,hom in [('DX11',0xb88040),('DX11AVX',0xb7ad70)]:
    exe=Path('C:/04_anomaly_MT/bin/Anomaly'+host+'.exe')
    identity=validate(exe,exe.with_suffix('.pdb'))
    old=json.loads((reference/f'evidence/MT_{host}_profile.json').read_text())
    assert identity['sha256']==old['sha256']
    info,read=pe_info(exe)
    functions=[old['functions'][2],old['functions'][1],{'rva':hom,'bytes':read(hom,24).hex()}]
    g=old['globals']
    vals=[g[k]for k in ['DEVICE','HW','RENDER','LEVEL','PERSISTENT','HDR','MSAA']]
    vals += [old['functions'][3]['rva'],old['functions'][4]['rva'],old['functions'][5]['rva']]
    fs=','.join('{%s,{%s}}'%(hex(f['rva']),','.join('0x'+f['bytes'][i:i+2]for i in range(0,48,2)))for f in functions)
    rows.append('{"%s","%s",%s,%s,{%s},%s}'%(host,identity['sha256'],hex(info['timestamp']),hex(info['image_size']),fs,','.join(hex(v)for v in vals)))
    (R/f'evidence/R1_{host}_identity.json').write_text(json.dumps(identity,indent=2))
header='''#pragma once
struct R1Function { unsigned rva; unsigned char bytes[24]; };
struct R1Host {const char* name;const char* hash;unsigned timestamp,image_size;R1Function hooks[3];unsigned device,hw,render,level,persistent,hdr,msaa,cache,cache_prev,menu;};
inline const R1Host R1_HOSTS[]={
'''+',\n'.join(rows)+'\n};\n'
(R/'native/R1_hosts.h').write_text(header)
print('Validated two exact PE/PDB pairs; generated three-hook signatures. Not runtime validation.')
