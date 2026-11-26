from pathlib import Path
import xml.etree.ElementTree as ET
import zipfile,hashlib,json
R=Path(__file__).parent
P=R/'package'
texts={
'eng':{'':'R1 XREAL Native','note':'Experimental MT DX11 stereo. Auto ON initially. F10 saves ON/OFF and controls automatic recovery. Esc closes game dialogs. UI stereo candidate. Disable TAA, HDR and MSAA.','toggle':'Toggle stereo','ipd':'Eye separation (mm)','convergence':'Convergence distance (m)'},
'rus':{'':'R1 XREAL Native','note':'Пробник стерео для MT DX11. Первый запуск: авто-ВКЛ. F10 сохраняет выбор и управляет автовосстановлением; Esc закрывает игровые окна. Тестовая стереокомпоновка интерфейса. Отключите TAA, HDR и MSAA.','toggle':'Переключить стерео','ipd':'Расстояние между глазами (мм)','convergence':'Дистанция сведения (м)'}}
texts['eng'].update(volume_note='Stereo volume sampling only. Lower values improve FPS but may reveal bands in light shafts. Disable the limit for original quality. Does not disable lights or shadows.',volume_optimize='Limit volumetric sample density',volume_quality='Maximum volume density (1.5 balanced)')
texts['rus'].update(volume_note='Плотность объёмного света только в стерео. Меньше — выше FPS, но возможны полосы в лучах. Выключите ограничение для исходного качества. Свет и тени не отключаются.',volume_optimize='Ограничить плотность объёмного света',volume_quality='Максимальная плотность (1.5 — баланс)')
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
z=R/'R1_XREAL_Native_MT_DX11_0.1.8_CANDIDATE.zip'
with zipfile.ZipFile(z,'w',zipfile.ZIP_DEFLATED) as archive:
 for p in files:archive.write(p,p.relative_to(P).as_posix())
with zipfile.ZipFile(z) as archive:
 assert archive.testzip() is None
 assert len(archive.namelist())==len(set(x.casefold() for x in archive.namelist()))
 for p in files:assert archive.read(p.relative_to(P).as_posix())==p.read_bytes()
manifest={'archive':z.name,'sha256':hashlib.sha256(z.read_bytes()).hexdigest(),'files':[{ 'path':p.relative_to(P).as_posix(),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}for p in files]}
(R/'evidence/R1_player_manifest.json').write_text(json.dumps(manifest,indent=2))
print(f'PASS ZIP CRC, unique paths, exact bytes, no test harness: {len(files)} files; {z}')
