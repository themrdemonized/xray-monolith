local root=assert(arg[1])
local callbacks={}
local installs,sets=0,0
local native_enabled,native_error=0,0
local values={toggle=68,ipd=64,convergence=2,volume_optimize=true,volume_quality=1.5}
local volume_cap
local dll={
 r1st_install=function() installs=installs+1;return 1 end,
 r1st_menu=function()return 1 end,
 r1st_volume_quality=function(cap)volume_cap=cap;return 1 end,
 r1st_set=function(on) sets=sets+1;if on==1 and native_enabled==0 then native_error=0 end;native_enabled=on;return 1 end,
 r1st_status=function(s) s[0].error=native_error;s[0].enabled=native_enabled;return 1 end
}
local fakeffi={cdef=function()end,load=function()return dll end,new=function()return {[0]={}}end,sizeof=function()return 32 end}
require=function(name) assert(name=='ffi');return fakeffi end
db={actor={alive=function()return true end}}
local screen_width=3840;local paused=false;device=function()return {width=screen_width,height=1080,is_paused=function()return paused end}end
local now=0;time_global=function()return now end
local camera=0;level={get_active_cam=function()return camera end}
local saved;io={open=function(_,mode) if mode=='rb' then if not saved then return nil end;return {read=function()return saved end,close=function()return true end} end;return {write=function(_,s)saved=s;return true end,close=function()return true end} end}
utils_xml={screen_ratio=function()return (1024/768)/(screen_width/1080)end}
utils_ui={UIInfoItem={InitControls=function()end}}
getFS=function()return {update_path=function(_,_,p)return p end}end
news_manager={send_tip=function()end}
printf=function()end
RegisterScriptCallback=function(k,f)callbacks[k]=f end
r1_xreal_native_mcm={get=function(k)return values[k]end}
DIK_keys={DIK_ESCAPE=1}
key_bindings={kINVENTORY=10,kPDA=11,kCAM_2=12,kCAM_3=13}
dik_to_bind=function(k)return k end
dofile(root..'/package/gamedata/scripts/r1_xreal_native.script')
local function tick(ms) now=now+(ms or 2100);callbacks.actor_on_update() end
on_game_start();tick();assert(installs==1 and native_enabled==1,'first run enables automatically')
assert(volume_cap==1.5)
callbacks.on_key_press(68);assert(native_enabled==0,'manual OFF')
tick();callbacks.load_state();tick();assert(native_enabled==0,'manual OFF survives load')
callbacks.on_key_press(68);assert(native_enabled==1,'manual ON')
callbacks.load_state();assert(native_enabled==0);tick();assert(native_enabled==1,'load restores intent')
callbacks.on_option_change();tick();assert(native_enabled==1,'options restore intent')
paused=true;tick();assert(native_enabled==0);paused=false;tick();assert(native_enabled==1,'unpause restores')
camera=1;tick();assert(native_enabled==0);tick();assert(native_enabled==0,'third person stays off')
camera=0;tick();assert(native_enabled==1,'first person restores')
native_error=-25;tick();assert(native_enabled==0);local count=sets;tick(100);assert(sets==count,'retry throttled');native_error=0;tick();assert(native_enabled==1)
native_enabled=0;tick();tick();assert(native_enabled==1,'native reset restores')
callbacks.on_key_press(1);assert(native_enabled==1,'Escape preserves stereo')
callbacks.on_key_press(10);assert(native_enabled==1,'inventory preserves stereo')
callbacks.actor_on_net_destroy();callbacks.on_key_press(68);tick();assert(native_enabled==0,'F10 while suspended cancels recovery')
dofile(root..'/package/gamedata/scripts/r1_xreal_native.script');on_game_start();tick();assert(native_enabled==0,'restart preserves manual OFF')
callbacks.on_key_press(68);assert(native_enabled==1);stop();dofile(root..'/package/gamedata/scripts/r1_xreal_native.script');on_game_start();tick();assert(native_enabled==1,'restart preserves ON')
stop();prepare_menu();assert(native_enabled==0,'menu does not enable geometry')
assert(math.abs(utils_xml.screen_ratio()-.75)<.00001,'ratio uses one eye');prepare_menu();assert(math.abs(utils_xml.screen_ratio()-.75)<.00001,'ratio not doubled twice');screen_width=1920;assert(math.abs(utils_xml.screen_ratio()-.75)<.00001,'normal screen preserved')
print('PASS desired state: auto start, F10, load, pause, options, camera, throttling, native reset, restart persistence and UI; mock only')
