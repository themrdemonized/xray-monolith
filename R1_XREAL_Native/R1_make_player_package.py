from pathlib import Path
import xml.etree.ElementTree as ET
import zipfile,hashlib,json
R=Path(__file__).parent
P=R/'package'
texts={
'eng':{'':'R1 XREAL Native','note':'Experimental MT DX11 stereo. OFF on load. F10 toggles. Esc/inventory/PDA stop stereo. 2D HUD is unavailable. Disable TAA, HDR and MSAA.','toggle':'Toggle stereo','ipd':'Eye separation (mm)','convergence':'Convergence distance (m)'},
'rus':{'':'R1 XREAL Native','note':'Пробник стерео для MT DX11. При загрузке выключен. F10 переключает режим; Esc, инвентарь и КПК выключают. 2D HUD не отображается. Отключите TAA, HDR и MSAA.','toggle':'Переключить стерео','ipd':'Расстояние между глазами (мм)','convergence':'Дистанция сведения (м)'}}
for lang,values in texts.items():
 root=ET.Element('string_table')
 for key,value in values.items():
  item=ET.SubElement(root,'string',id='ui_mcm_r1_xreal_native'+('_'+key if key else ''))
  ET.SubElement(item,'text').text=value
 target=P/f'gamedata/configs/text/{lang}/st_r1_xreal_native.xml'
 target.parent.mkdir(parents=True,exist_ok=True)
 target.write_bytes(ET.tostring(root,encoding='windows-1251',xml_declaration=True))
 ET.parse(target)
allowed={'.dll','.script','.xml','.md','.txt'}
files=[p for p in P.rglob('*') if p.is_file()]
assert all(p.suffix in allowed for p in files), 'Unexpected package artifacts'
assert not any('live' in p.name.lower() or 'test' in p.name.lower() for p in files),'Test harness in player ZIP'
z=R/'R1_XREAL_Native_MT_DX11_0.1.1_EXPERIMENTAL.zip'
with zipfile.ZipFile(z,'w',zipfile.ZIP_DEFLATED) as archive:
 for p in files:archive.write(p,p.relative_to(P).as_posix())
with zipfile.ZipFile(z) as archive:
 assert archive.testzip() is None
 assert len(archive.namelist())==len(set(x.casefold() for x in archive.namelist()))
 for p in files:assert archive.read(p.relative_to(P).as_posix())==p.read_bytes()
manifest={'archive':z.name,'sha256':hashlib.sha256(z.read_bytes()).hexdigest(),'files':[{ 'path':p.relative_to(P).as_posix(),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}for p in files]}
(R/'evidence/R1_player_manifest.json').write_text(json.dumps(manifest,indent=2))
print(f'PASS ZIP CRC, unique paths, exact bytes, no test harness: {len(files)} files; {z}')
