from pathlib import Path
import shutil,re
R=Path(__file__).parent
G=Path('C:/04_anomaly_MT')
seed=Path('C:/0_CHATGPT_ANOMALY/work/r1_echo_mt_20260912/live/DX11_native_001')
root=R/'live/root';app=root/'appdata'
assert not root.exists(), 'Preserve existing run'
(root/'bin').mkdir(parents=True)
for directory in ['logs','savedgames','screenshots']:(app/directory).mkdir(parents=True)
shutil.copytree(G/'gamedata',root/'gamedata')
for path in (G/'bin').iterdir():
    if path.suffix.lower()=='.dll' or path.name in ['AnomalyDX11.exe','alsoft.ini']:
        if path.name.lower()!='dxgi.dll':shutil.copy2(path,root/'bin'/path.name)
shutil.copytree(R/'package/gamedata',root/'gamedata',dirs_exist_ok=True)
for f in (seed/'root/appdata/savedgames').glob('r1_look_normal.*'):shutil.copy2(f,app/'savedgames'/f.name)
shutil.copy2(R/'tests/R1_live.script',root/'gamedata/scripts/R1_live.script')
settings=(seed/'root/appdata/user.ltx').read_text('cp1251')
for key,value in {'vid_mode':'2560x720','rs_screenmode':'windowed','snd_volume_eff':'0','snd_volume_music':'0','ssfx_taa':'(0,0,0,0)'}.items():
    settings=re.sub(r'^'+re.escape(key)+r' .+$',key+' '+value,settings,flags=re.M)
(app/'user.ltx').write_text(settings,'cp1251')
fs=(seed/'fsgame.ltx').read_text('cp1251')
fs=re.sub(r'^\$fs_root\$.*$',lambda _:'$fs_root$ = false | false | '+str(root)+'\\bin\\..\\',fs,flags=re.M)
(R/'live/R1_fsgame.ltx').write_text(fs,'cp1251')
(root/'bin/alsoft.ini').write_text('[general]\ndrivers=null\n')
print(root)
