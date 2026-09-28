local root=assert(arg[1])
local callbacks={}
local installs,sets=0,0
local native_enabled,native_error=0,0
local values={toggle=68,ipd=64,convergence=2}
local dll={
 r1st_install=function() installs=installs+1;return 1 end,
 r1st_set=function(on) sets=sets+1;if on==1 and native_enabled==0 then native_error=0 end;native_enabled=on;return 1 end,
 r1st_status=function(s) s[0].error=native_error;return 1 end
}
local fakeffi={cdef=function()end,load=function()return dll end,new=function()return {[0]={}}end,sizeof=function()return 32 end}
require=function(name) assert(name=='ffi');return fakeffi end
db={actor={alive=function()return true end}}
device=function()return {is_paused=function()return false end}end
getFS=function()return {update_path=function(_,_,p)return p end}end
news_manager={send_tip=function()end}
printf=function()end
RegisterScriptCallback=function(k,f)callbacks[k]=f end
r1_xreal_native_mcm={get=function(k)return values[k]end}
DIK_keys={DIK_ESCAPE=1}
key_bindings={kINVENTORY=10,kPDA=11,kCAM_2=12,kCAM_3=13}
dik_to_bind=function(k)return k end
dofile(root..'/package/gamedata/scripts/r1_xreal_native.script')
on_game_start();callbacks.actor_on_update();assert(installs==0 and sets==0,'default-off must not load DLL')
callbacks.on_key_press(68);assert(installs==1 and native_enabled==1)
callbacks.actor_on_update();callbacks.on_key_press(1);assert(native_enabled==0,'Escape stops')
callbacks.on_key_press(68);native_error=-25;callbacks.actor_on_update();assert(native_enabled==0,'TAA refusal stops')
callbacks.on_key_press(68);callbacks.actor_on_update();assert(native_enabled==1,'retry after correcting a mode')
callbacks.on_option_change();assert(native_enabled==0)
callbacks.on_key_press(68);callbacks.actor_on_net_destroy();assert(native_enabled==0)
callbacks.on_key_press(68);callbacks.on_key_press(10);assert(native_enabled==0,'inventory stops')
assert(installs==1,'hooks must not install repeatedly')
print('PASS loader default-off, toggle, Escape, native refusal, retry, settings, destroy, inventory; mock only')
