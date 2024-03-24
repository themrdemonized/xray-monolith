////////////////////////////////////////////////////////////////////////////
//	Module 		: level_script.cpp
//	Created 	: 28.06.2004
//  Modified 	: 28.06.2004
//	Author		: Dmitriy Iassenev
//	Description : Level script export
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "level.h"
#include "actor.h"
#include "script_game_object.h"
#include "patrol_path_storage.h"
#include "xrServer.h"
#include "client_spawn_manager.h"
#include "../xrEngine/igame_persistent.h"
#include "game_cl_base.h"
#include "UIGameCustom.h"
#include "UI/UIDialogWnd.h"
#include "date_time.h"
#include "ai_space.h"
#include "level_graph.h"
#include "PHCommander.h"
#include "PHScriptCall.h"
#include "script_engine.h"
#include "game_cl_single.h"
#include "game_sv_single.h"
#include "map_manager.h"
#include "map_spot.h"
#include "map_location.h"
#include "physics_world_scripted.h"
#include "alife_simulator.h"
#include "alife_time_manager.h"
#include "UI/UIGameTutorial.h"
#include "string_table.h"
#include "ui/UIInventoryUtilities.h"
#include "alife_object_registry.h"
#include "xrServer_Objects_ALife_Monsters.h"
#include "hudmanager.h"
#include "ui\UIMainIngameWnd.h"
#include "ui\UIHudStatesWnd.h"
#include "raypick.h"
#include "../xrcdb/xr_collide_defs.h"
#include "../xrEngine/Rain.h"

#include "../xrEngine/xr_efflensflare.h"
#include "../xrEngine/thunderbolt.h"
#include "GametaskManager.h"
#include "xr_level_controller.h"
#include "../xrEngine/GameMtlLib.h"
#include "../xrEngine/xr_input.h"
#include "script_ini_file.h"
#include "EffectorBobbing.h"
#include "LevelDebugScript.h"

#include "ui\UIPdaMsgListItem.h"
#include "ui\UILogsWnd.h"
#include "game_news.h"
#include "alife_registry_wrappers.h"

using namespace luabind;

extern ENGINE_API float ps_r2_sun_shafts_min;
extern ENGINE_API float ps_r2_sun_shafts_value;
bool g_block_all_except_movement;
bool g_actor_allow_ladder = true;

LPCSTR command_line()
{
	return (Core.Params);
}

bool IsDynamicMusic()
{
	return !!psActorFlags.test(AF_DYNAMIC_MUSIC);
}

bool IsImportantSave()
{
	return !!psActorFlags.test(AF_IMPORTANT_SAVE);
}

#ifdef DEBUG
void check_object(CScriptGameObject *object)
{
	try {
		Msg	("check_object %s",object->Name());
	}
	catch(...) {
		object = object;
	}
}

CScriptGameObject *tpfGetActor()
{
	static bool first_time = true;
	if (first_time)
		ai().script_engine().script_log(eLuaMessageTypeError,"Do not use level.actor function!");
	first_time = false;
	
	CActor *l_tpActor = smart_cast<CActor*>(Level().CurrentEntity());
	if (l_tpActor)
		return	(smart_cast<CGameObject*>(l_tpActor)->lua_game_object());
	else
		return	(0);
}

CScriptGameObject *get_object_by_name(LPCSTR caObjectName)
{
	static bool first_time = true;
	if (first_time)
		ai().script_engine().script_log(eLuaMessageTypeError,"Do not use level.object function!");
	first_time = false;
	
	CGameObject		*l_tpGameObject	= smart_cast<CGameObject*>(Level().Objects.FindObjectByName(caObjectName));
	if (l_tpGameObject)
		return		(l_tpGameObject->lua_game_object());
	else
		return		(0);
}
#endif

// demonized: add u16 id version of function to improve performance
CScriptGameObject* get_object_by_id(u16 id)
{
	CGameObject* pGameObject = smart_cast<CGameObject*>(Level().Objects.net_Find(id));
	if (!pGameObject)
		return nullptr;

	return pGameObject->lua_game_object();
}

CScriptGameObject* get_object_by_id()
{
	Msg("!WARNING : level.object_by_id(nil) called!");
	ai().script_engine().print_stack();
	return nullptr;
}

CScriptGameObject* get_object_by_id(const luabind::object& ob)
{
	if (!ob || ob.type() == LUA_TNIL)
	{
		Msg("!WARNING : level.object_by_id(nil) called!");
		ai().script_engine().print_stack();
		return nullptr;
	}

	u16 id = luabind::object_cast<u16>(ob);
	return get_object_by_id(id);
}

LPCSTR get_weather()
{
	return (*g_pGamePersistent->Environment().GetWeather());
}

void set_weather(LPCSTR weather_name, bool forced)
{
#ifdef INGAME_EDITOR
	if (!Device.editor())
#endif // #ifdef INGAME_EDITOR
	g_pGamePersistent->Environment().SetWeather(weather_name, forced);
}

bool set_weather_fx(LPCSTR weather_name)
{
#ifdef INGAME_EDITOR
	if (!Device.editor())
#endif // #ifdef INGAME_EDITOR
	return (g_pGamePersistent->Environment().SetWeatherFX(weather_name));

#ifdef INGAME_EDITOR
	return			(false);
#endif // #ifdef INGAME_EDITOR
}

bool start_weather_fx_from_time(LPCSTR weather_name, float time)
{
#ifdef INGAME_EDITOR
	if (!Device.editor())
#endif // #ifdef INGAME_EDITOR
	return (g_pGamePersistent->Environment().StartWeatherFXFromTime(weather_name, time));

#ifdef INGAME_EDITOR
	return			(false);
#endif // #ifdef INGAME_EDITOR
}

bool is_wfx_playing()
{
	return (g_pGamePersistent->Environment().IsWFXPlaying());
}

float get_wfx_time()
{
	return (g_pGamePersistent->Environment().wfx_time);
}

void stop_weather_fx()
{
	g_pGamePersistent->Environment().StopWFX();
}

void set_time_factor(float time_factor)
{
	if (!OnServer())
		return;

#ifdef INGAME_EDITOR
	if (Device.editor())
		return;
#endif // #ifdef INGAME_EDITOR

	Level().Server->game->SetGameTimeFactor(time_factor);
}

float get_time_factor()
{
	return (Level().GetGameTimeFactor());
}

void set_game_difficulty(ESingleGameDifficulty dif)
{
	g_SingleGameDifficulty = dif;
	game_cl_Single* game = smart_cast<game_cl_Single*>(Level().game);
	VERIFY(game);
	game->OnDifficultyChanged();
}

ESingleGameDifficulty get_game_difficulty()
{
	return g_SingleGameDifficulty;
}

u32 get_time_days()
{
	u32 year = 0, month = 0, day = 0, hours = 0, mins = 0, secs = 0, milisecs = 0;
	split_time((g_pGameLevel && Level().game) ? Level().GetGameTime() : ai().alife().time_manager().game_time(), year,
	           month, day, hours, mins, secs, milisecs);
	return day;
}

u32 get_time_hours()
{
	u32 year = 0, month = 0, day = 0, hours = 0, mins = 0, secs = 0, milisecs = 0;
	split_time((g_pGameLevel && Level().game) ? Level().GetGameTime() : ai().alife().time_manager().game_time(), year,
	           month, day, hours, mins, secs, milisecs);
	return hours;
}

u32 get_time_minutes()
{
	u32 year = 0, month = 0, day = 0, hours = 0, mins = 0, secs = 0, milisecs = 0;
	split_time((g_pGameLevel && Level().game) ? Level().GetGameTime() : ai().alife().time_manager().game_time(), year,
	           month, day, hours, mins, secs, milisecs);
	return mins;
}

void change_game_time(u32 days, u32 hours, u32 mins)
{
	game_sv_Single* tpGame = smart_cast<game_sv_Single *>(Level().Server->game);
	if (tpGame && ai().get_alife())
	{
		u32 value = days * 86400 + hours * 3600 + mins * 60;
		float fValue = static_cast<float>(value);
		value *= 1000; //msec		
		g_pGamePersistent->Environment().ChangeGameTime(fValue);
		tpGame->alife().time_manager().change_game_time(value);
	}
}

float high_cover_in_direction(u32 level_vertex_id, const Fvector& direction)
{
	if (!ai().level_graph().valid_vertex_id(level_vertex_id))
	{
		return 0;
	}

	float y, p;
	direction.getHP(y, p);
	return (ai().level_graph().high_cover_in_direction(y, level_vertex_id));
}

float low_cover_in_direction(u32 level_vertex_id, const Fvector& direction)
{
	if (!ai().level_graph().valid_vertex_id(level_vertex_id))
	{
		return 0;
	}

	float y, p;
	direction.getHP(y, p);
	return (ai().level_graph().low_cover_in_direction(y, level_vertex_id));
}

float rain_factor()
{
	return (g_pGamePersistent->Environment().CurrentEnv->rain_density);
}

float rain_wetness()
{
	return (g_pGamePersistent->Environment().wetness_factor);
}

float rain_hemi()
{
	CEffect_Rain* rain = g_pGamePersistent->pEnvironment->eff_Rain;

	if (rain)
	{
		return rain->GetRainHemi();
	}
	else
	{
		CObject* E = g_pGameLevel->CurrentViewEntity();
		if (E && E->renderable_ROS())
		{
			float* hemi_cube = E->renderable_ROS()->get_luminocity_hemi_cube();
			float hemi_val = _max(hemi_cube[0], hemi_cube[1]);
			hemi_val = _max(hemi_val, hemi_cube[2]);
			hemi_val = _max(hemi_val, hemi_cube[3]);
			hemi_val = _max(hemi_val, hemi_cube[5]);

			return hemi_val;
		}
		return 0;
	}
}

u32 vertex_in_direction(u32 level_vertex_id, Fvector direction, float max_distance)
{
	if (!ai().level_graph().valid_vertex_id(level_vertex_id))
	{
		return u32(-1);
	}
	direction.normalize_safe();
	direction.mul(max_distance);
	Fvector start_position = ai().level_graph().vertex_position(level_vertex_id);
	Fvector finish_position = Fvector(start_position).add(direction);
	u32 result = u32(-1);
	ai().level_graph().farthest_vertex_in_direction(level_vertex_id, start_position, finish_position, result, 0);
	return (ai().level_graph().valid_vertex_id(result) ? result : level_vertex_id);
}

Fvector vertex_position(u32 level_vertex_id)
{
	if (!ai().level_graph().valid_vertex_id(level_vertex_id))
	{
		return Fvector{};
	}
	return (ai().level_graph().vertex_position(level_vertex_id));
}

void map_add_object_spot(u16 id, LPCSTR spot_type, LPCSTR text)
{
	CMapLocation* ml = Level().MapManager().AddMapLocation(spot_type, id);
	if (xr_strlen(text))
	{
		ml->SetHint(text);
	}
}

void map_add_object_spot_ser(u16 id, LPCSTR spot_type, LPCSTR text)
{
	CMapLocation* ml = Level().MapManager().AddMapLocation(spot_type, id);
	if (xr_strlen(text))
		ml->SetHint(text);

	ml->SetSerializable(true);
}

void map_change_spot_hint(u16 id, LPCSTR spot_type, LPCSTR text)
{
	CMapLocation* ml = Level().MapManager().GetMapLocation(spot_type, id);
	if (!ml) return;
	ml->SetHint(text);
}

void map_remove_object_spot(u16 id, LPCSTR spot_type)
{
	Level().MapManager().RemoveMapLocation(spot_type, id);
}

// demonized: remove all map object spots by id
void map_remove_all_object_spots(u16 id)
{
	Level().MapManager().RemoveAllMapLocationsById(id);
}

CUIStatic* map_get_spot_static(u16 id, LPCSTR spot_type)
{
	CMapLocation* ml = Level().MapManager().GetMapLocation(spot_type, id);
	if (!ml) return nullptr;
	CUIStatic* map_spot_static = ml->LevelMapSpotNC();
	return map_spot_static;
}
CUIStatic* map_get_minimap_spot_static(u16 id, LPCSTR spot_type)
{
	CMapLocation* ml = Level().MapManager().GetMapLocation(spot_type, id);
	if (!ml) return nullptr;
	CUIStatic* map_spot_static = ml->MiniMapSpotNC();
	return map_spot_static;
}

u16 map_has_object_spot(u16 id, LPCSTR spot_type)
{
	return Level().MapManager().HasMapLocation(spot_type, id);
}

bool patrol_path_exists(LPCSTR patrol_path)
{
	return (!!ai().patrol_paths().path(patrol_path, true));
}

LPCSTR get_name()
{
	return (*Level().name());
}

void prefetch_sound(LPCSTR name)
{
	Level().PrefetchSound(name);
}


CClientSpawnManager& get_client_spawn_manager()
{
	return (Level().client_spawn_manager());
}

/*
void start_stop_menu(CUIDialogWnd* pDialog, bool bDoHideIndicators)
{
	if(pDialog->IsShown())
		pDialog->HideDialog();
	else
		pDialog->ShowDialog(bDoHideIndicators);
}
*/

void add_dialog_to_render(CUIDialogWnd* pDialog)
{
	CurrentGameUI()->AddDialogToRender(pDialog);
}

void remove_dialog_to_render(CUIDialogWnd* pDialog)
{
	CurrentGameUI()->RemoveDialogToRender(pDialog);
}

void hide_indicators()
{
	if (CurrentGameUI())
	{
		CurrentGameUI()->HideShownDialogs();
		CurrentGameUI()->ShowGameIndicators(false);
		CurrentGameUI()->ShowCrosshair(false);
	}
	psActorFlags.set(AF_GODMODE_RT, TRUE);
}

void hide_indicators_safe()
{
	if (CurrentGameUI())
	{
		CurrentGameUI()->ShowGameIndicators(false);
		CurrentGameUI()->ShowCrosshair(false);

		CurrentGameUI()->OnExternalHideIndicators();
	}
	psActorFlags.set(AF_GODMODE_RT, TRUE);
}

void show_indicators()
{
	if (CurrentGameUI())
	{
		CurrentGameUI()->ShowGameIndicators(true);
		CurrentGameUI()->ShowCrosshair(true);
	}
	psActorFlags.set(AF_GODMODE_RT, FALSE);
}

void show_weapon(bool b)
{
	psHUD_Flags.set(HUD_WEAPON_RT2, b);
}

bool is_level_present()
{
	return (!!g_pGameLevel);
}

void add_call(const luabind::functor<bool>& condition, const luabind::functor<void>& action)
{
	luabind::functor<bool> _condition = condition;
	luabind::functor<void> _action = action;
	CPHScriptCondition* c = xr_new<CPHScriptCondition>(_condition);
	CPHScriptAction* a = xr_new<CPHScriptAction>(_action);
	Level().ph_commander_scripts().add_call(c, a);
}

void remove_call(const luabind::functor<bool>& condition, const luabind::functor<void>& action)
{
	CPHScriptCondition c(condition);
	CPHScriptAction a(action);
	Level().ph_commander_scripts().remove_call(&c, &a);
}

void add_call(const luabind::object& lua_object, LPCSTR condition, LPCSTR action)
{
	//	try{	
	//		CPHScriptObjectCondition	*c=xr_new<CPHScriptObjectCondition>(lua_object,condition);
	//		CPHScriptObjectAction		*a=xr_new<CPHScriptObjectAction>(lua_object,action);
	luabind::functor<bool> _condition = object_cast<luabind::functor<bool>>(lua_object[condition]);
	luabind::functor<void> _action = object_cast<luabind::functor<void>>(lua_object[action]);
	CPHScriptObjectConditionN* c = xr_new<CPHScriptObjectConditionN>(lua_object, _condition);
	CPHScriptObjectActionN* a = xr_new<CPHScriptObjectActionN>(lua_object, _action);
	Level().ph_commander_scripts().add_call_unique(c, c, a, a);
	//	}
	//	catch(...)
	//	{
	//		Msg("add_call excepted!!");
	//	}
}

void remove_call(const luabind::object& lua_object, LPCSTR condition, LPCSTR action)
{
	CPHScriptObjectCondition c(lua_object, condition);
	CPHScriptObjectAction a(lua_object, action);
	Level().ph_commander_scripts().remove_call(&c, &a);
}

void add_call(const luabind::object& lua_object, const luabind::functor<bool>& condition,
              const luabind::functor<void>& action)
{
	CPHScriptObjectConditionN* c = xr_new<CPHScriptObjectConditionN>(lua_object, condition);
	CPHScriptObjectActionN* a = xr_new<CPHScriptObjectActionN>(lua_object, action);
	Level().ph_commander_scripts().add_call(c, a);
}

void remove_call(const luabind::object& lua_object, const luabind::functor<bool>& condition,
                 const luabind::functor<void>& action)
{
	CPHScriptObjectConditionN c(lua_object, condition);
	CPHScriptObjectActionN a(lua_object, action);
	Level().ph_commander_scripts().remove_call(&c, &a);
}

void remove_calls_for_object(const luabind::object& lua_object)
{
	CPHSriptReqObjComparer c(lua_object);
	Level().ph_commander_scripts().remove_calls(&c);
}

cphysics_world_scripted* physics_world_scripted()
{
	return get_script_wrapper<cphysics_world_scripted>(*physics_world());
}

CEnvironment* environment()
{
	return (g_pGamePersistent->pEnvironment);
}

CEnvDescriptor* current_environment(CEnvironment* self)
{
	return (self->CurrentEnv);
}

extern bool g_bDisableAllInput;

void disable_input()
{
	// "unpress" all keys when we disable level input! (but keep input devices aquired)
	pInput->DeactivateSoft();
	g_bDisableAllInput = true;
#ifdef DEBUG
	Msg("input disabled");
#endif // #ifdef DEBUG
}

void enable_input()
{
	g_bDisableAllInput = false;
#ifdef DEBUG
	Msg("input enabled");
#endif // #ifdef DEBUG
}

void spawn_phantom(const Fvector& position)
{
	Level().spawn_item("m_phantom", position, u32(-1), u16(-1), false);
}

Fbox get_bounding_volume()
{
	return Level().ObjectSpace.GetBoundingVolume();
}

void iterate_sounds(LPCSTR prefix, u32 max_count, const CScriptCallbackEx<void>& callback)
{
	for (int j = 0, N = _GetItemCount(prefix); j < N; ++j)
	{
		string_path fn, s;
		LPSTR S = (LPSTR)&s;
		_GetItem(prefix, j, s);
		if (FS.exist(fn, "$game_sounds$", S, ".ogg"))
			callback(prefix);

		for (u32 i = 0; i < max_count; ++i)
		{
			string_path name;
			xr_sprintf(name, "%s%d", S, i);
			if (FS.exist(fn, "$game_sounds$", name, ".ogg"))
				callback(name);
		}
	}
}

void iterate_sounds1(LPCSTR prefix, u32 max_count, luabind::functor<void> functor)
{
	CScriptCallbackEx<void> temp;
	temp.set(functor);
	iterate_sounds(prefix, max_count, temp);
}

void iterate_sounds2(LPCSTR prefix, u32 max_count, luabind::object object, luabind::functor<void> functor)
{
	CScriptCallbackEx<void> temp;
	temp.set(functor, object);
	iterate_sounds(prefix, max_count, temp);
}

#include "actoreffector.h"

float add_cam_effector(LPCSTR fn, int id, bool cyclic, LPCSTR cb_func)
{
	CAnimatorCamEffectorScriptCB* e = xr_new<CAnimatorCamEffectorScriptCB>(cb_func);
	e->SetType((ECamEffectorType)id);
	e->SetCyclic(cyclic);
	e->Start(fn);
	Actor()->Cameras().AddCamEffector(e);
	return e->GetAnimatorLength();
}

float add_cam_effector(LPCSTR fn, int id, bool cyclic, LPCSTR cb_func, float cam_fov)
{
	CAnimatorCamEffectorScriptCB* e = xr_new<CAnimatorCamEffectorScriptCB>(cb_func);
	if (cam_fov)
	{
		e->m_bAbsolutePositioning = true;
		e->m_fov = cam_fov;
	}
	e->SetType((ECamEffectorType)id);
	e->SetCyclic(cyclic);
	e->Start(fn);
	Actor()->Cameras().AddCamEffector(e);
	return e->GetAnimatorLength();
}

float add_cam_effector(LPCSTR fn, int id, bool cyclic, LPCSTR cb_func, float cam_fov, bool b_hud)
{
	CAnimatorCamEffectorScriptCB* e = xr_new<CAnimatorCamEffectorScriptCB>(cb_func);
	if (cam_fov)
	{
		e->m_bAbsolutePositioning = true;
		e->m_fov = cam_fov;
	}
	e->SetHudAffect(b_hud);
	e->SetType((ECamEffectorType)id);
	e->SetCyclic(cyclic);
	e->Start(fn);
	Actor()->Cameras().AddCamEffector(e);
	return e->GetAnimatorLength();
}

float add_cam_effector(LPCSTR fn, int id, bool cyclic, LPCSTR cb_func, float cam_fov, bool b_hud, float power)
{
	CAnimatorCamEffectorScriptCB* e = xr_new<CAnimatorCamEffectorScriptCB>(cb_func);
	if (cam_fov)
	{
		e->m_bAbsolutePositioning = true;
		e->m_fov = cam_fov;
	}
	if (power)
	{
		e->SetPower(power);
	}
	e->SetHudAffect(b_hud);
	e->SetType((ECamEffectorType)id);
	e->SetCyclic(cyclic);
	e->Start(fn);
	Actor()->Cameras().AddCamEffector(e);
	return e->GetAnimatorLength();
}

// demonized: Get cam effector transform data from "*.anm" file
#include "../xrEngine/motion.h"
#include "../xrEngine/envelope.h"
bool getCamEffectorTransformData(luabind::object& t, LPCSTR animationFile)
{
	string_path full_path;
	if (!FS.exist(full_path, "$level$", animationFile))
		if (!FS.exist(full_path, "$game_anims$", animationFile)) {
			Msg("![getCamEffectorTransformData] Can't find motion file '%s'.", animationFile);
			return false;
		}

	LPCSTR ext = strext(full_path);
	if (ext)
	{
		if (0 == xr_strcmp(ext, ".anm"))
		{
			COMotion M;
			if (M.LoadMotion(full_path)) {
				std::map<EChannelType, std::string> mapOrder;
				mapOrder[EChannelType::ctPositionX] = "positionX";
				mapOrder[EChannelType::ctPositionY] = "positionY";
				mapOrder[EChannelType::ctPositionZ] = "positionZ";
				mapOrder[EChannelType::ctRotationH] = "positionH";
				mapOrder[EChannelType::ctRotationP] = "positionP";
				mapOrder[EChannelType::ctRotationB] = "positionB";

				for (const auto& p : mapOrder) {
					auto k = p.first;
					auto v = p.second;
					xr_vector<st_Key*>& keys = M.envs[k]->keys;
					luabind::object keyData = luabind::newtable(ai().script_engine().lua());
					int i = 1;
					for (KeyIt k_it = keys.begin(); k_it != keys.end(); k_it++) {
						luabind::object data = luabind::newtable(ai().script_engine().lua());
						data["time"] = (*k_it)->time;
						data["value"] = (*k_it)->value;
						data["shape"] = (*k_it)->shape;
						data["tension"] = (*k_it)->tension;
						data["continuity"] = (*k_it)->continuity;
						data["bias"] = (*k_it)->bias;
						data["param0"] = (*k_it)->param[0];
						data["param1"] = (*k_it)->param[1];
						data["param2"] = (*k_it)->param[2];
						data["param3"] = (*k_it)->param[3];
						keyData[i] = data;
						i++;
					}
					t[v] = keyData;
				}

				return true;
			} else {
				Msg("![getCamEffectorTransformData] failed to load file '%s'.", animationFile);
				return false;
			}
		} else {
			Msg("![getCamEffectorTransformData] file has incorrect extension '%s'.", animationFile);
			return false;
		}
	}
	Msg("![getCamEffectorTransformData] unknown error, file '%s'.", animationFile);
	return false;
}

// demonized: Set custom camera position and direction with movement smoothing (for cutscenes, etc)
void set_cam_position_direction(Fvector& position, Fvector& direction, unsigned int smoothing, bool hudEnabled, bool hudAffect)
{
	CActor* actor = Actor();
	actor->initFPCam();
	actor->m_FPCam->m_HPB.set(direction);
	actor->m_FPCam->m_Position.set(position);
	actor->m_FPCam->m_customSmoothing = smoothing;
	actor->m_FPCam->hudEnabled = hudEnabled;
	actor->m_FPCam->SetHudAffect(hudAffect);
}

void set_cam_position_direction(Fvector& position, Fvector& direction)
{
	set_cam_position_direction(position, direction, 1, false, false);
}

void set_cam_position_direction(Fvector& position, Fvector& direction, unsigned int smoothing)
{
	set_cam_position_direction(position, direction, smoothing, false, false);
}

void set_cam_position_direction(Fvector& position, Fvector& direction, unsigned int smoothing, bool hudEnabled)
{
	set_cam_position_direction(position, direction, smoothing, hudEnabled, false);
}

void remove_cam_position_direction() 
{
	CActor* actor = Actor();
	actor->removeFPCam();
}

void remove_cam_effector(int id)
{
	Actor()->Cameras().RemoveCamEffector((ECamEffectorType)id);
}

void set_cam_effector_factor(int id, float factor)
{
	CAnimatorCamEffectorScriptCB* e = smart_cast<CAnimatorCamEffectorScriptCB*>(Actor()->Cameras().GetCamEffector((ECamEffectorType)id));
	if (e)
		e->SetPower(factor);
}

float get_cam_effector_factor(int id)
{
	CAnimatorCamEffectorScriptCB* e = smart_cast<CAnimatorCamEffectorScriptCB*>(Actor()->Cameras().GetCamEffector((ECamEffectorType)id));
	return e ? e->GetPower() : 0.0f;
}

float get_cam_effector_length(int id)
{
	CAnimatorCamEffectorScriptCB* e = smart_cast<CAnimatorCamEffectorScriptCB*>(Actor()->Cameras().GetCamEffector((ECamEffectorType)id));
	return e ? e->GetAnimatorLength() : 0.0f;
}


bool check_cam_effector(int id)
{
	CAnimatorCamEffectorScriptCB* e = smart_cast<CAnimatorCamEffectorScriptCB*>(Actor()->Cameras().GetCamEffector((ECamEffectorType)id));
	if (e)
	{
		return e->Valid();
	}
	return false;
}


float get_snd_volume()
{
	return psSoundVFactor;
}

float get_rain_volume()
{
	CEffect_Rain* rain = g_pGamePersistent->pEnvironment->eff_Rain;
	return rain ? rain->GetRainVolume() : 0.0f;
}

void set_snd_volume(float v)
{
	psSoundVFactor = v;
	clamp(psSoundVFactor, 0.0f, 1.0f);
}

float get_music_volume() {
	return psSoundVMusicFactor;
}

void set_music_volume(float v) {
	psSoundVMusicFactor = v;
	clamp(psSoundVMusicFactor, 0.0f, 1.0f);
}

#include "actor_statistic_mgr.h"

void add_actor_points(LPCSTR sect, LPCSTR detail_key, int cnt, int pts)
{
	return Actor()->StatisticMgr().AddPoints(sect, detail_key, cnt, pts);
}

void add_actor_points_str(LPCSTR sect, LPCSTR detail_key, LPCSTR str_value)
{
	return Actor()->StatisticMgr().AddPoints(sect, detail_key, str_value);
}

int get_actor_points(LPCSTR sect)
{
	return Actor()->StatisticMgr().GetSectionPoints(sect);
}


#include "ActorEffector.h"

void add_complex_effector(LPCSTR section, int id)
{
	AddEffector(Actor(), id, section);
}

void remove_complex_effector(int id)
{
	RemoveEffector(Actor(), id);
}

#include "postprocessanimator.h"

void add_pp_effector(LPCSTR fn, int id, bool cyclic)
{
	CPostprocessAnimator* pp = xr_new<CPostprocessAnimator>(id, cyclic);
	pp->Load(fn);
	Actor()->Cameras().AddPPEffector(pp);
}

void remove_pp_effector(int id)
{
	CPostprocessAnimator* pp = smart_cast<CPostprocessAnimator*>(Actor()->Cameras().GetPPEffector((EEffectorPPType)id));

	if (pp) pp->Stop(1.0f);
}

void set_pp_effector_factor(int id, float f, float f_sp)
{
	CPostprocessAnimator* pp = smart_cast<CPostprocessAnimator*>(Actor()->Cameras().GetPPEffector((EEffectorPPType)id));

	if (pp) pp->SetDesiredFactor(f, f_sp);
}

void set_pp_effector_factor2(int id, float f)
{
	CPostprocessAnimator* pp = smart_cast<CPostprocessAnimator*>(Actor()->Cameras().GetPPEffector((EEffectorPPType)id));

	if (pp) pp->SetCurrentFactor(f);
}

#include "relation_registry.h"

int g_community_goodwill(LPCSTR _community, int _entity_id)
{
	CHARACTER_COMMUNITY c;
	c.set(_community);

	return RELATION_REGISTRY().GetCommunityGoodwill(c.index(), u16(_entity_id));
}

void g_set_community_goodwill(LPCSTR _community, int _entity_id, int val)
{
	CHARACTER_COMMUNITY c;
	c.set(_community);
	RELATION_REGISTRY().SetCommunityGoodwill(c.index(), u16(_entity_id), val);
}

void g_change_community_goodwill(LPCSTR _community, int _entity_id, int val)
{
	CHARACTER_COMMUNITY c;
	c.set(_community);
	RELATION_REGISTRY().ChangeCommunityGoodwill(c.index(), u16(_entity_id), val);
}

int g_get_community_relation(LPCSTR comm_from, LPCSTR comm_to)
{
	CHARACTER_COMMUNITY community_from;
	community_from.set(comm_from);
	CHARACTER_COMMUNITY community_to;
	community_to.set(comm_to);

	return RELATION_REGISTRY().GetCommunityRelation(community_from.index(), community_to.index());
}

void g_set_community_relation(LPCSTR comm_from, LPCSTR comm_to, int value)
{
	CHARACTER_COMMUNITY community_from;
	community_from.set(comm_from);
	CHARACTER_COMMUNITY community_to;
	community_to.set(comm_to);

	RELATION_REGISTRY().SetCommunityRelation(community_from.index(), community_to.index(), value);
}

int g_get_general_goodwill_between(u16 from, u16 to)
{
	CHARACTER_GOODWILL presonal_goodwill = RELATION_REGISTRY().GetGoodwill(from, to);
	VERIFY(presonal_goodwill != NO_GOODWILL);

	CSE_ALifeTraderAbstract* from_obj = smart_cast<CSE_ALifeTraderAbstract*>(ai().alife().objects().object(from));
	CSE_ALifeTraderAbstract* to_obj = smart_cast<CSE_ALifeTraderAbstract*>(ai().alife().objects().object(to));

	if (!from_obj || !to_obj)
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "RELATION_REGISTRY::get_general_goodwill_between  : cannot convert obj to CSE_ALifeTraderAbstract!");
		return (0);
	}
	CHARACTER_GOODWILL community_to_obj_goodwill = RELATION_REGISTRY().GetCommunityGoodwill(from_obj->Community(), to);
	CHARACTER_GOODWILL community_to_community_goodwill = RELATION_REGISTRY().GetCommunityRelation(
		from_obj->Community(), to_obj->Community());

	return presonal_goodwill + community_to_obj_goodwill + community_to_community_goodwill;
}

void refresh_npc_names()
{
	CALifeObjectRegistry::OBJECT_REGISTRY alobjs = ai().alife().objects().objects();
	CALifeObjectRegistry::OBJECT_REGISTRY::iterator it = alobjs.begin();
	CALifeObjectRegistry::OBJECT_REGISTRY::iterator it_e = alobjs.end();

	for (; it != it_e; it++)
	{
		CSE_ALifeTraderAbstract* tr = smart_cast<CSE_ALifeTraderAbstract*>(it->second);
		if (tr)
		{
			tr->m_character_name = TranslateName(tr->m_character_name_str.c_str());

			if (g_pGameLevel)
			{
				CObject* obj = g_pGameLevel->Objects.net_Find(it->first);
				CInventoryOwner* owner = smart_cast<CInventoryOwner*>(obj);
				if (owner)
					owner->refresh_npc_name();
			}
		}
	}

	if (psDeviceFlags2.test(rsDiscord))
	{
		Actor()->RPC_UpdateFaction();
		Actor()->RPC_UpdateRank();
		Actor()->RPC_UpdateReputation();

		Level().GameTaskManager().RPC_UpdateTaskName();
	}
}


void LevelPressAction(int cmd)
{
	if ((cmd == MOUSE_1 || cmd == MOUSE_2) && !!GetSystemMetrics(SM_SWAPBUTTON))
		cmd = cmd == MOUSE_1 ? MOUSE_2 : MOUSE_1;

	Level().IR_OnKeyboardPress(cmd);
}

void LevelReleaseAction(int cmd)
{
	if ((cmd == MOUSE_1 || cmd == MOUSE_2) && !!GetSystemMetrics(SM_SWAPBUTTON))
		cmd = cmd == MOUSE_1 ? MOUSE_2 : MOUSE_1;

	Level().IR_OnKeyboardRelease(cmd);
}

void LevelHoldAction(int cmd)
{
	if ((cmd == MOUSE_1 || cmd == MOUSE_2) && !!GetSystemMetrics(SM_SWAPBUTTON))
		cmd = cmd == MOUSE_1 ? MOUSE_2 : MOUSE_1;

	Level().IR_OnKeyboardHold(cmd);
}

u32 vertex_id(Fvector position)
{
	return (ai().level_graph().vertex_id(position));
}

u32 render_get_dx_level()
{
	return ::Render->get_dx_level();
}

CUISequencer* g_tutorial = NULL;
CUISequencer* g_tutorial2 = NULL;

void start_tutorial(LPCSTR name)
{
	if (g_tutorial)
	{
		VERIFY(!g_tutorial2);
		g_tutorial2 = g_tutorial;
	};

	g_tutorial = xr_new<CUISequencer>();
	g_tutorial->Start(name);
	if (g_tutorial2)
		g_tutorial->m_pStoredInputReceiver = g_tutorial2->m_pStoredInputReceiver;
}

void stop_tutorial()
{
	if (g_tutorial)
		g_tutorial->Stop();
}

LPCSTR translate_string(LPCSTR str)
{
	return *CStringTable().translate(str);
}

bool has_active_tutotial()
{
	return (g_tutorial != NULL);
}

float get_weather_value_numric(LPCSTR name)
{
	CEnvDescriptor& E = *environment()->CurrentEnv;

	if (0 == xr_strcmp(name, "sky_rotation"))
		return E.sky_rotation;
	else if (0 == xr_strcmp(name, "far_plane"))
		return E.far_plane;
	else if (0 == xr_strcmp(name, "fog_density"))
		return E.fog_density;
	else if (0 == xr_strcmp(name, "fog_distance"))
		return E.fog_distance;
	else if (0 == xr_strcmp(name, "rain_density"))
		return E.rain_density;
	else if (0 == xr_strcmp(name, "thunderbolt_period"))
		return E.bolt_period;
	else if (0 == xr_strcmp(name, "thunderbolt_duration"))
		return E.bolt_duration;
	else if (0 == xr_strcmp(name, "wind_velocity"))
		return E.wind_velocity;
	else if (0 == xr_strcmp(name, "wind_direction"))
		return E.wind_direction;
	else if (0 == xr_strcmp(name, "sun_shafts_intensity"))
		return E.m_fSunShaftsIntensity;
	else if (0 == xr_strcmp(name, "water_intensity"))
		return E.m_fWaterIntensity;
	else if (0 == xr_strcmp(name, "tree_amplitude_intensity"))
		return E.m_fTreeAmplitudeIntensity;
	else if (0 == xr_strcmp(name, "volumetric_intensity_factor"))
		return E.volumetric_intensity_factor;
	else if (0 == xr_strcmp(name, "volumetric_distance_factor"))
		return E.volumetric_distance_factor;

	return (0);
}

void set_weather_value_numric(LPCSTR name, float val)
{
	CEnvDescriptorMixer& E = *environment()->CurrentEnv;

	if (0 == xr_strcmp(name, "sky_rotation"))
		E.sky_rotation = val;
	else if (0 == xr_strcmp(name, "far_plane"))
		E.far_plane = val * psVisDistance;
	else if (0 == xr_strcmp(name, "fog_density"))
	{
		E.fog_density = val;
		E.fog_near = (1.0f - E.fog_density) * 0.85f * E.fog_distance;
	}
	else if (0 == xr_strcmp(name, "fog_distance"))
	{
		E.fog_distance = val;
		clamp(E.fog_distance, 1.f, E.far_plane - 10);
		E.fog_near = (1.0f - E.fog_density) * 0.85f * E.fog_distance;
		E.fog_far = 0.99f * E.fog_distance;
	}
	else if (0 == xr_strcmp(name, "rain_density"))
		E.rain_density = val;
	else if (0 == xr_strcmp(name, "thunderbolt_period"))
		E.bolt_period = val;
	else if (0 == xr_strcmp(name, "thunderbolt_duration"))
		E.bolt_duration = val;
	else if (0 == xr_strcmp(name, "wind_velocity"))
		E.wind_velocity = val;
	else if (0 == xr_strcmp(name, "wind_direction"))
		E.wind_direction = deg2rad(val);
	else if (0 == xr_strcmp(name, "sun_shafts_intensity"))
	{
		E.m_fSunShaftsIntensity = val;
		E.m_fSunShaftsIntensity *= 1.0f - ps_r2_sun_shafts_min;
		E.m_fSunShaftsIntensity += ps_r2_sun_shafts_min;
		E.m_fSunShaftsIntensity *= ps_r2_sun_shafts_value;
		clamp(E.m_fSunShaftsIntensity, 0.0f, 1.0f);
	}
	else if (0 == xr_strcmp(name, "water_intensity"))
		E.m_fWaterIntensity = val;
	else if (0 == xr_strcmp(name, "tree_amplitude_intensity"))
		E.m_fTreeAmplitudeIntensity = val;
	else if (0 == xr_strcmp(name, "volumetric_intensity_factor"))
		E.volumetric_intensity_factor = val;
	else if (0 == xr_strcmp(name, "volumetric_distance_factor"))
		E.volumetric_distance_factor = val;
	else
		Msg("~xrGame\level_script.cpp (set_weather_value_numric) | [%s] is not a valid numric weather parameter to set", name);
}

Fvector3 get_weather_value_vector(LPCSTR name)
{
	CEnvDescriptor& E = *environment()->CurrentEnv;

	if (0 == xr_strcmp(name, "sky_color"))
		return E.sky_color;
	else if (0 == xr_strcmp(name, "fog_color"))
		return E.fog_color;
	else if (0 == xr_strcmp(name, "rain_color"))
		return E.rain_color;
	else if (0 == xr_strcmp(name, "ambient_color"))
		return E.ambient;
	else if (0 == xr_strcmp(name, "sun_color"))
		return E.sun_color;
	else if (0 == xr_strcmp(name, "sun_dir"))
		return E.sun_dir;

	Fvector3 vec;
	vec.set(0, 0, 0);

	if (0 == xr_strcmp(name, "clouds_color"))
	{
		Fvector4 temp = E.clouds_color;
		vec.set(temp.x, temp.y, temp.z);
	}
	else if (0 == xr_strcmp(name, "hemisphere_color"))
	{
		Fvector4 temp = E.hemi_color;
		vec.set(temp.x, temp.y, temp.z);
	}

	return vec;
}

void set_weather_value_vector(LPCSTR name, float x, float y, float z, float w = 0)
{
	CEnvDescriptor& E = *environment()->CurrentEnv;

	if (0 == xr_strcmp(name, "sky_color"))
		E.sky_color.set(x, y, z);
	else if (0 == xr_strcmp(name, "fog_color"))
		E.fog_color.set(x, y, z);
	else if (0 == xr_strcmp(name, "rain_color"))
		E.rain_color.set(x, y, z);
	else if (0 == xr_strcmp(name, "ambient_color"))
		E.ambient.set(x, y, z);
	else if (0 == xr_strcmp(name, "sun_color"))
		E.sun_color.set(x, y, z);
	else if (0 == xr_strcmp(name, "clouds_color"))
		E.clouds_color.set(x, y, z, w);
	else if (0 == xr_strcmp(name, "hemisphere_color"))
		E.hemi_color.set(x, y, z, w);
	else
		Msg("~xrGame\level_script.cpp (set_weather_value_vector) | [%s] is not a valid vector weather parameter to set", name);
}

LPCSTR get_weather_value_string(LPCSTR name)
{
	CEnvDescriptor& E = *environment()->CurrentEnv;

	if (0 == xr_strcmp(name, "clouds_texture"))
		return E.clouds_texture_name.c_str();

	else if (0 == xr_strcmp(name, "sky_texture"))
		return E.sky_texture_name.c_str();

	else if (0 == xr_strcmp(name, "ambient"))
		return E.env_ambient->name().c_str();

	return "";
}

void set_weather_value_string(LPCSTR name, LPCSTR newval)
{
	CEnvDescriptor& E = *environment()->CurrentEnv;

	if (0 == xr_strcmp(name, "clouds_texture"))
	{
		if (E.clouds_texture_name._get() != shared_str(newval)._get())
		{
			E.m_pDescriptor->OnDeviceDestroy();
			E.clouds_texture_name = newval;
			E.m_pDescriptor->OnDeviceCreate(E);
		}
	}
	else if (0 == xr_strcmp(name, "sky_texture"))
	{
		if (E.sky_texture_name._get() != shared_str(newval)._get())
		{
			string_path st_env;
			strconcat(sizeof(st_env), st_env, newval, "#small");
			E.m_pDescriptor->OnDeviceDestroy();
			E.sky_texture_name = newval;
			E.sky_texture_env_name = st_env;
			E.m_pDescriptor->OnDeviceCreate(E);
		}
	}
	else if (0 == xr_strcmp(name, "sun"))
	{
		E.lens_flare_id = environment()->eff_LensFlare->AppendDef(*environment(), environment()->m_suns_config, newval);
	}
	else if (0 == xr_strcmp(name, "thunderbolt_collection"))
	{
		E.tb_id = environment()->eff_Thunderbolt->AppendDef(*environment(),
		                                                    environment()->m_thunderbolt_collections_config,
		                                                    environment()->m_thunderbolts_config, newval);
	}
	else if (0 == xr_strcmp(name, "ambient"))
	{
		E.env_ambient = environment()->AppendEnvAmb(newval);
	}
	else
		Msg("~xrGame\level_script.cpp (set_weather_value_string) | [%s] is not a valid string weather parameter to set", name);
}

void pause_weather(bool b_pause)
{
	environment()->m_paused = b_pause;
}

bool is_weather_paused()
{
	return environment()->m_paused;
}

void reload_weather()
{
	environment()->Reload();
}

void boost_weather_value(LPCSTR name, float value)
{
	if (0 == xr_strcmp(name, "ambient_color"))
		environment()->env_boost.ambient = value;
	else if (0 == xr_strcmp(name, "hemisphere_color"))
		environment()->env_boost.hemi = value;
	else if (0 == xr_strcmp(name, "fog_color"))
		environment()->env_boost.fog_color = value;
	else if (0 == xr_strcmp(name, "rain_color"))
		environment()->env_boost.rain_color = value;
	else if (0 == xr_strcmp(name, "sky_color"))
		environment()->env_boost.sky_color = value;
	else if (0 == xr_strcmp(name, "clouds_color"))
		environment()->env_boost.clouds_color = value;
	else if (0 == xr_strcmp(name, "sun_color"))
		environment()->env_boost.sun_color = value;
	else
		Msg("~xrGame\level_script.cpp (boost_weather_value)| [%s] is not a valid weather parameter to boost", name);
}

void boost_weather_reset()
{
	environment()->env_boost.ambient = 0.f;
	environment()->env_boost.hemi = 0.f;
	environment()->env_boost.fog_color = 0.f;
	environment()->env_boost.rain_color = 0.f;
	environment()->env_boost.sky_color = 0.f;
	environment()->env_boost.sun_color = 0.f;
}

void sun_time(int hour, int minute)
{
	float real_sun_alt, real_sun_long;
	float s_alt = environment()->sun_hp[hour].x;
	float s_long = environment()->sun_hp[hour].y;

	if (minute > 0)
	{
		float s_weight = minute / 60.f;
		int next_hour = hour == 23 ? 0 : hour + 1;
		float s_alt2 = environment()->sun_hp[next_hour].x;
		float s_long2 = environment()->sun_hp[next_hour].y;

		real_sun_alt = _lerp(s_alt, s_alt2, s_weight);
		real_sun_long = _lerp(s_long, s_long2, s_weight);
	}
	else
	{
		real_sun_alt = s_alt;
		real_sun_long = s_long;
	}

	R_ASSERT(_valid(real_sun_alt));
	R_ASSERT(_valid(real_sun_long));

	CEnvDescriptor& E = *environment()->CurrentEnv;
	E.sun_dir.setHP(
		deg2rad(real_sun_alt),
		deg2rad(real_sun_long)
	);

	R_ASSERT(_valid(E.sun_dir));
}

void reload_language()
{
	CStringTable().ReloadLanguage();
}

#include "player_hud.h"

void hud_adj_offs(int off, int idx, float x, float y, float z)
{
	// Script UI
	if (idx == 20)
	{
		g_player_hud->m_adjust_ui_offset[off].set(x, y, z);

		if (off == 1)
			g_player_hud->m_adjust_ui_offset[1].mul(PI / 180.f);
	}

	// Fire point/dir ; shell point
	else if (idx == 10 || idx == 11)
	{
		g_player_hud->m_adjust_firepoint_shell[off][idx-10].set(x, y, z);
	}

	// Object pos/dir
	else if (idx == 12)
	{
		g_player_hud->m_adjust_obj[off].set(x, y, z);
	}

	// Hud offsets
	else
		g_player_hud->m_adjust_offset[off][idx].set(x, y, z);
}

#include "Inventory.h"
#include "Weapon.h"

void hud_adj_value(LPCSTR name, float val)
{
	if (0 == xr_strcmp(name, "scope_zoom_factor"))
		g_player_hud->m_adjust_zoom_factor[0] = val;
	else if (0 == xr_strcmp(name, "gl_zoom_factor"))
		g_player_hud->m_adjust_zoom_factor[1] = val;
	else if (0 == xr_strcmp(name, "scope_zoom_factor_alt"))
		g_player_hud->m_adjust_zoom_factor[2] = val;
}

void hud_adj_state(bool state)
{
	g_player_hud->m_adjust_mode = state;
}

LPCSTR vid_modes_string()
{
	xr_string resolutions = "";

	xr_token* tok = vid_mode_token;
	while (tok->name)
	{
		if (strlen(resolutions.c_str()) > 0)
			resolutions.append(",");

		resolutions.append(tok->name);
		tok++;
	}

	return resolutions.c_str();
}

u32 PlayHudMotion(u8 hand, LPCSTR itm_name, LPCSTR anm_name, bool bMixIn = true, float speed = 1.f)
{
	return g_player_hud->script_anim_play(hand, itm_name, anm_name, bMixIn, speed);
}

void StopHudMotion()
{
	g_player_hud->StopScriptAnim();
}

float MotionLength(LPCSTR section, LPCSTR name, float speed)
{
	return g_player_hud->motion_length_script(section, name, speed);
}

bool AllowHudMotion()
{
	return g_player_hud->allow_script_anim();
}

void PlayBlendAnm(LPCSTR name, u8 part, float speed, float power, bool bLooped, bool no_restart)
{
	g_player_hud->PlayBlendAnm(name, part, speed, power, bLooped, no_restart);
}

void StopBlendAnm(LPCSTR name, bool bForce)
{
	g_player_hud->StopBlendAnm(name, bForce);
}

void StopAllBlendAnms(bool bForce)
{
	g_player_hud->StopAllBlendAnms(bForce);
}

float SetBlendAnmTime(LPCSTR name, float time)
{
	return g_player_hud->SetBlendAnmTime(name, time);
}

void block_all_except_movement(bool b)
{
	g_block_all_except_movement = b;
}

bool only_movement_allowed()
{
	return g_block_all_except_movement;
}

void set_actor_allow_ladder(bool b)
{
	g_actor_allow_ladder = b;
}

void set_nv_lumfactor(float factor)
{
	g_pGamePersistent->nv_shader_data.lum_factor = factor;
}

void remove_hud_model(LPCSTR section)
{
	LPCSTR hud_section = READ_IF_EXISTS(pSettings, r_string, section, "hud", nullptr);
	if (hud_section != nullptr)
		g_player_hud->remove_from_model_pool(hud_section);
	else
		Msg("can't find hud section for [%s]", section);
}

const u32 ActorMovingState()
{
	return g_actor->MovingState();
}

extern ENGINE_API float psHUD_FOV;

const Fvector2 world2ui(Fvector pos, bool hud = false, bool allow_offscreen = false)
{
	Fmatrix world, res;
	world.identity();
	world.c = pos;

	if (hud)
	{
		Fmatrix FP, FT, FV;
		FV.build_camera_dir(Device.vCameraPosition, Device.vCameraDirection, Device.vCameraTop);
		FP.build_projection(
			deg2rad(psHUD_FOV * 83.f),
			Device.fASPECT, R_VIEWPORT_NEAR,
			g_pGamePersistent->Environment().CurrentEnv->far_plane);

		FT.mul(FP, FV);
		res.mul(FT, world);
	}
	else
		res.mul(Device.mFullTransform, world);
	
	Fvector4 v_res;

	v_res.w = res._44;
	v_res.x = res._41 / v_res.w;
	v_res.y = res._42 / v_res.w;
	v_res.z = res._43 / v_res.w;

	if (!allow_offscreen)
	{
		if (v_res.z < 0 || v_res.w < 0) return { -9999,0 };
		if (abs(v_res.x) > 1.f || abs(v_res.y) > 1.f) return { -9999,0 };
	}

	float x = (1.f + v_res.x) / 2.f * (Device.dwWidth);
	float y = (1.f - v_res.y) / 2.f * (Device.dwHeight);

	float width_fk = Device.dwWidth / UI_BASE_WIDTH;
	float height_fk = Device.dwHeight / UI_BASE_HEIGHT;

	x /= width_fk;
	y /= height_fk;

	return { x,y };
}

// demonized: unproject ui coordinates (ie mouse cursor coordinates) to world coordinates
// returns position and underlying object id if found. If there is no object, obj_id will be 65535
void ui2world(Fvector2 pos, Fvector& res, u16& obj_id)
{
	res.set(0, 0, 0);
	if (pos.x < 0 || pos.x > UI_BASE_WIDTH || pos.y < 0 || pos.y > UI_BASE_HEIGHT) {
		return;
	}

	// Convert to [-1; 1] NDC space
	pos.x = 2 * pos.x / UI_BASE_WIDTH - 1;
	pos.y = 1 - 2 * pos.y / UI_BASE_HEIGHT;

	Fmatrix mProject = Device.mFullTransform;

	// 4x4 invert of camera matrix
	{
		Fmatrix& m = mProject;

		float mProjectDet = m._11 * (m._22*m._33*m._44 + m._23*m._34*m._42 + m._24*m._32*m._43 - m._24*m._33*m._42 - m._23*m._32*m._44 - m._22*m._34*m._43)
						-   m._21 * (m._12*m._33*m._44 + m._13*m._34*m._42 + m._14*m._32*m._43 - m._14*m._33*m._42 - m._13*m._32*m._44 - m._12*m._34*m._43)
						+   m._31 * (m._12*m._23*m._44 + m._13*m._24*m._42 + m._14*m._22*m._43 - m._14*m._23*m._42 - m._13*m._22*m._44 - m._12*m._24*m._43)
						-   m._41 * (m._12*m._23*m._34 + m._13*m._24*m._32 + m._14*m._22*m._33 - m._14*m._23*m._32 - m._13*m._22*m._34 - m._12*m._24*m._33);

		Fmatrix mProjectAdjugate;
		mProjectAdjugate._11 = m._22*m._33*m._44 + m._23*m._34*m._42 + m._24*m._32*m._43 - m._24*m._33*m._42 - m._23*m._32*m._44 - m._22*m._34*m._43;
		mProjectAdjugate._12 = -m._12*m._33*m._44 - m._13*m._34*m._42 - m._14*m._32*m._43 + m._14*m._33*m._42 + m._13*m._32*m._44 + m._12*m._34*m._43;
		mProjectAdjugate._13 = m._12*m._23*m._44 + m._13*m._24*m._42 + m._14*m._22*m._43 - m._14*m._23*m._42 - m._13*m._22*m._44 - m._12*m._24*m._43;
		mProjectAdjugate._14 = -m._12*m._23*m._34 - m._13*m._24*m._32 - m._14*m._22*m._33 + m._14*m._23*m._32 + m._13*m._22*m._34 + m._12*m._24*m._33;

		mProjectAdjugate._21 = -m._21*m._33*m._44 - m._23*m._34*m._41 - m._24*m._31*m._43 + m._24*m._33*m._41 + m._23*m._31*m._44 + m._21*m._34*m._43;
		mProjectAdjugate._22 = m._11*m._33*m._44 + m._13*m._34*m._41 + m._14*m._31*m._43 - m._14*m._33*m._41 - m._13*m._31*m._44 - m._11*m._34*m._43;
		mProjectAdjugate._23 = -m._11*m._23*m._44 - m._13*m._24*m._41 - m._14*m._21*m._43 + m._14*m._23*m._41 + m._13*m._21*m._44 + m._11*m._24*m._43;
		mProjectAdjugate._24 = m._11*m._23*m._34 + m._13*m._24*m._31 + m._14*m._21*m._33 - m._14*m._23*m._31 - m._13*m._21*m._34 - m._11*m._24*m._33;

		mProjectAdjugate._31 = m._21*m._32*m._44 + m._22*m._34*m._41 + m._24*m._31*m._42 - m._24*m._32*m._41 - m._22*m._31*m._44 - m._21*m._34*m._42;
		mProjectAdjugate._32 = -m._11*m._32*m._44 - m._12*m._34*m._41 - m._14*m._31*m._42 + m._14*m._32*m._41 + m._12*m._31*m._44 + m._11*m._34*m._42;
		mProjectAdjugate._33 = m._11*m._22*m._44 + m._12*m._24*m._41 + m._14*m._21*m._42 - m._14*m._22*m._41 - m._12*m._21*m._44 - m._11*m._24*m._42;
		mProjectAdjugate._34 = -m._11*m._22*m._34 - m._12*m._24*m._31 - m._14*m._21*m._32 + m._14*m._22*m._31 + m._12*m._21*m._34 + m._11*m._24*m._32;

		mProjectAdjugate._41 = -m._21*m._32*m._43 - m._22*m._33*m._41 - m._23*m._31*m._42 + m._23*m._32*m._41 + m._22*m._31*m._43 + m._21*m._33*m._42;
		mProjectAdjugate._42 = m._11*m._32*m._43 + m._12*m._33*m._41 + m._13*m._31*m._42 - m._13*m._32*m._41 - m._12*m._31*m._43 - m._11*m._33*m._42;
		mProjectAdjugate._43 = -m._11*m._22*m._43 - m._12*m._23*m._41 - m._13*m._21*m._42 + m._13*m._22*m._41 + m._12*m._21*m._43 + m._11*m._23*m._42;
		mProjectAdjugate._44 = m._11*m._22*m._33 + m._12*m._23*m._31 + m._13*m._21*m._32 - m._13*m._22*m._31 - m._12*m._21*m._33 - m._11*m._23*m._32;

		mProjectDet = 1.0 / mProjectDet;
		mProjectAdjugate.mul(mProjectDet);
		mProject.set(mProjectAdjugate);
	}
		
	// get position at arbitrary depth
	res.set(pos.x, pos.y, 1);
	{
		auto& e = mProject.m;
		auto x = res.x;
		auto y = res.y;
		auto z = res.z;
		float w = 1.0 / (e[0][3] * x + e[1][3] * y + e[2][3] * z + e[3][3]);

		res.x = (e[0][0] * x + e[1][0] * y + e[2][0] * z + e[3][0]) * w;
		res.y = (e[0][1] * x + e[1][1] * y + e[2][1] * z + e[3][1]) * w;
		res.z = (e[0][2] * x + e[1][2] * y + e[2][2] * z + e[3][2]) * w;
	}

	// perform ray cast to get actual position
	collide::rq_result R;
	CObject* ignore = Actor();
	Fvector start = Device.vCameraPosition;
	Fvector dir;
	dir.set(res).sub(start).normalize();
	start.mad(dir, R_VIEWPORT_NEAR);
	float range = g_pGamePersistent->Environment().CurrentEnv->far_plane;

	obj_id = 65535;
	if (Level().ObjectSpace.RayPick(start, dir, range, collide::rqtBoth, R, ignore))
	{
		res.mad(start, dir, R.range);

		if (R.O) {
			CGameObject* o = smart_cast<CGameObject*>(R.O);
			if (o) {
				obj_id = o->ID();
			}
		}
	}
}

void ui2world(Fvector& pos, Fvector& res, u16& obj_id)
{
	ui2world(Fvector2().set(pos.x, pos.y), res, obj_id);
}

const float get_env_rads()
{
	if (!CurrentGameUI())
		return 0.f;

	return CurrentGameUI()->UIMainIngameWnd->get_hud_states()->get_main_sensor_value();
}

//Alundaio: namespace level exports extension
#ifdef NAMESPACE_LEVEL_EXPORTS
//ability to update level netpacket
void g_send(NET_Packet& P, bool bReliable = 0, bool bSequential = 1, bool bHighPriority = 0, bool bSendImmediately = 0)
{
	Level().Send(P, net_flags(bReliable, bSequential, bHighPriority, bSendImmediately));
}

//can spawn entities like bolts, phantoms, ammo, etc. which normally crash when using alife():create()
void spawn_section(LPCSTR sSection, Fvector3 vPosition, u32 LevelVertexID, u16 ParentID, bool bReturnItem = false)
{
	Level().spawn_item(sSection, vPosition, LevelVertexID, ParentID, bReturnItem);
}

//ability to get the target game_object at crosshair
CScriptGameObject* g_get_target_obj()
{
	collide::rq_result& RQ = HUD().GetCurrentRayQuery();
	if (RQ.O)
	{
		CGameObject* game_object = static_cast<CGameObject*>(RQ.O);
		if (game_object)
			return game_object->lua_game_object();
	}
	return (0);
}

float g_get_target_dist()
{
	collide::rq_result& RQ = HUD().GetCurrentRayQuery();
	if (RQ.range)
		return RQ.range;
	return (0);
}

u32 g_get_target_element()
{
	collide::rq_result& RQ = HUD().GetCurrentRayQuery();
	if (RQ.element)
	{
		return RQ.element;
	}
	return (0);
}

// demonized: get world position under crosshair
Fvector g_get_target_pos()
{
	collide::rq_result& RQ = HUD().GetCurrentRayQuery();
	if (RQ.range)
	{
		return Fvector().mad(Device.vCameraPosition, Device.vCameraDirection, RQ.range);
	}
	return Fvector().set(0, 0, 0);
}

// demonized: get result of crosshair ray query
script_rq_result g_get_target_result()
{
	collide::rq_result& RQ = HUD().GetCurrentRayQuery();
	auto script_rq = script_rq_result();
	if (RQ.range) {
		script_rq.set(RQ);
	}
	return script_rq;
}

u8 get_active_cam()
{
	CActor* actor = smart_cast<CActor*>(Level().CurrentViewEntity());
	if (actor)
		return (u8)actor->active_cam();

	return 255;
}

void set_active_cam(u8 mode)
{
	CActor* actor = smart_cast<CActor*>(Level().CurrentViewEntity());
	if (actor && mode <= ACTOR_DEFS::EActorCameras::eacMaxCam)
		actor->cam_Set((ACTOR_DEFS::EActorCameras)mode);
}

void reload_hud_xml()
{
	HUD().OnScreenResolutionChanged();
}

bool actor_safemode()
{
	return Actor()->is_safemode();
}

void actor_set_safemode(bool status)
{
	if (Actor()->is_safemode() != status)
	{
		CWeapon* wep = smart_cast<CWeapon*>(Actor()->inventory().ActiveItem());
		if (wep && wep->m_bCanBeLowered)
		{
			wep->Action(kSAFEMODE, CMD_START);
			Actor()->set_safemode(status);
		}
	}
}

void prefetch_texture(LPCSTR name)
{
	Device.m_pRender->ResourcesPrefetchCreateTexture( name );
}

void prefetch_model(LPCSTR name)
{
	::Render->models_PrefetchOne(name);
}
#endif
//-Alundaio

// KD: raypick	
bool ray_pick(const Fvector& start, const Fvector& dir, float range, collide::rq_target tgt, script_rq_result& script_R,
              CScriptGameObject* ignore_object)
{
	collide::rq_result R;
	CObject* ignore = NULL;
	if (ignore_object)
		ignore = smart_cast<CObject *>(&(ignore_object->object()));
	if (Level().ObjectSpace.RayPick(start, dir, range, tgt, R, ignore))
	{
		script_R.set(R);
		return true;
	}
	else
		return false;
}

CScriptGameObject* get_view_entity_script()
{
	CGameObject* pGameObject = smart_cast<CGameObject*>(Level().CurrentViewEntity());
	if (!pGameObject)
		return (0);

	return pGameObject->lua_game_object();
}

void set_view_entity_script(CScriptGameObject* go)
{
	CObject* o = smart_cast<CObject*>(&go->object());
	if (o)
		Level().SetViewEntity(o);
}

xrTime get_start_time()
{
	return (xrTime(Level().GetStartGameTime()));
}

void iterate_nearest(const Fvector& pos, float radius, const luabind::functor<bool>& functor)
{
	xr_vector<CObject*> m_nearest;
	Level().ObjectSpace.GetNearest(m_nearest, pos, radius, NULL);

	if (!m_nearest.size()) return;

	// demonized: sort nearest by distance first
	std::sort(m_nearest.begin(), m_nearest.end(), [&pos](CObject* o1, CObject* o2) {
		return o1->Position().distance_to_sqr(pos) < o2->Position().distance_to_sqr(pos);
	});

	xr_vector<CObject*>::iterator it = m_nearest.begin();
	xr_vector<CObject*>::iterator it_e = m_nearest.end();
	for (; it != it_e; it++)
	{
		CGameObject* obj = smart_cast<CGameObject*>(*it);
		if (!obj) continue;
		if (functor(obj->lua_game_object())) break;
	}
}

LPCSTR PickMaterial(const Fvector& start_pos, const Fvector& dir, float trace_dist, CScriptGameObject* ignore_obj)
{
	collide::rq_result result;
	BOOL reach_wall =
		Level().ObjectSpace.RayPick(
			start_pos,
			dir,
			trace_dist,
			collide::rqtStatic,
			result,
			ignore_obj ? &ignore_obj->object() : nullptr
		)
		&&
		!result.O;

	if (reach_wall)
	{
		CDB::TRI* pTri = Level().ObjectSpace.GetStaticTris() + result.element;
		SGameMtl* pMaterial = GMLib.GetMaterialByIdx(pTri->material);

		if (pMaterial)
		{
			return *pMaterial->m_Name;
		}
	}

	return "$null";
}

CScriptIniFile* GetVisualUserdata(LPCSTR visual)
{
	string_path low_name, fn;

	VERIFY(xr_strlen(visual) < sizeof(low_name));
	xr_strcpy(low_name, visual);
	strlwr(low_name);

	if (strext(low_name)) *strext(low_name) = 0;
	xr_strcat(low_name, sizeof(low_name), ".ogf");

	if (!FS.exist(low_name))
	{
		if (!FS.exist(fn, "$level$", low_name))
		{
			if (!FS.exist(fn, "$game_meshes$", low_name))
			{
				Msg("!Can't find model file '%s'.", low_name);
				return nullptr;
			}
		}
	}
	else
	{
		xr_strcpy(fn, low_name);
	}

	IReader* data = FS.r_open(fn);
	if (!data) return nullptr;

	IReader* UD = data->open_chunk(17); //OGF_S_USERDATA
	if (!UD) return nullptr;

	CScriptIniFile* ini = xr_new<CScriptIniFile>(UD, FS.get_path("$game_config$")->m_Path);
	FS.r_close(data);
	UD->close();

	return ini;
}

DBG_ScriptObject* get_object(u16 id)
{
	xr_map<u16, DBG_ScriptObject*>::iterator it = Level().getScriptRenderQueue()->find(id);
	if (it == Level().getScriptRenderQueue()->end())
		return nullptr;

	return it->second;
}

void remove_object(u16 id)
{
	DBG_ScriptObject* dbg_obj = get_object(id);
	if (!dbg_obj)
		return;

	xr_delete(dbg_obj);
	Level().getScriptRenderQueue()->erase(id);
}

DBG_ScriptObject* add_object(u16 id, DebugRenderType type)
{
	remove_object(id);
	DBG_ScriptObject* dbg_obj = nullptr;

	switch (type)
	{
	case eDBGSphere:
		dbg_obj = xr_new<DBG_ScriptSphere>();
		break;
	case eDBGBox:
		dbg_obj = xr_new<DBG_ScriptBox>();
		break;
	case eDBGLine:
		dbg_obj = xr_new<DBG_ScriptLine>();
		break;
	default:
		R_ASSERT2(false, "Wrong debug object type used!");
	}

	R_ASSERT(dbg_obj);
	Level().getScriptRenderQueue()->emplace(mk_pair(id, dbg_obj));

	return dbg_obj;
}

u32 get_flags()
{
	return Level().m_debug_render_flags.get();
}

void set_flags(u32 flags)
{
	Level().m_debug_render_flags.assign(flags);
}

// demonized: adjust game news time
void change_game_news_show_time(CUIWindow* CUIWindowPItem, float show_time) {
	if (!CUIWindowPItem) return;

	auto pItem = smart_cast<CUIPdaMsgListItem*>(CUIWindowPItem);
	if (!pItem) return;

	pItem->ResetColorAnimation();
	pItem->SetColorAnimation("ui_main_msgs_short", LA_ONLYALPHA | LA_TEXTCOLOR | LA_TEXTURECOLOR, show_time);
}

void update_pda_news_from_uiwindow(CUIWindow* CUIWindowPItem) {
	if (!CUIWindowPItem) return;

	auto pItem = smart_cast<CUIPdaMsgListItem*>(CUIWindowPItem);
	if (!pItem) return;

	auto news = pItem->getNews();
	if (!news) {
		Msg("![update_pda_news_from_uiwindow] cannot get news from pItem");
	}

	GAME_NEWS_VECTOR& news_vector = Actor()->game_news_registry->registry().objects();
	GAME_NEWS_IT newsVectorRes = std::find_if(news_vector.begin(), news_vector.end(), [&news](const GAME_NEWS_DATA& d) {
		return d.receive_time == news->receive_time;
	});
	if (newsVectorRes != news_vector.end()) {
		newsVectorRes->news_caption = pItem->UICaptionText.GetText();
		newsVectorRes->news_text = pItem->UIMsgText.GetText();
		newsVectorRes->texture_name = pItem->UIIcon.m_TextureName.c_str();
		CurrentGameUI()->UpdatePda();
	}
	else {
		Msg("![update_pda_news_from_uiwindow] cannot find news by text %s", news->news_text.c_str());
	}
}

#pragma optimize("s",on)
void CLevel::script_register(lua_State* L)
{
	module(L)
		[
			class_<CEnvDescriptor>("CEnvDescriptor")
			.def_readonly("fog_density", &CEnvDescriptor::fog_density)
		.def_readonly("far_plane", &CEnvDescriptor::far_plane),

		class_<CEnvironment>("CEnvironment")
		.def("current", current_environment),

		class_<DBG_ScriptObject>("DBG_ScriptObject")
		.enum_("dbg_type")
		[
			value("line", (int)DebugRenderType::eDBGLine),
			value("sphere", (int)DebugRenderType::eDBGSphere),
			value("box", (int)DebugRenderType::eDBGBox)
		]
	.def("cast_dbg_sphere", &DBG_ScriptObject::cast_dbg_sphere)
		.def("cast_dbg_box", &DBG_ScriptObject::cast_dbg_box)
		.def("cast_dbg_line", &DBG_ScriptObject::cast_dbg_line)
		.def_readwrite("color", &DBG_ScriptObject::m_color)
		.def_readwrite("hud", &DBG_ScriptObject::m_hud)
		.def_readwrite("visible", &DBG_ScriptObject::m_visible),

		class_<DBG_ScriptSphere, DBG_ScriptObject>("DBG_ScriptSphere")
		.def_readwrite("matrix", &DBG_ScriptSphere::m_mat),

		class_<DBG_ScriptBox, DBG_ScriptObject>("DBG_ScriptBox")
		.def_readwrite("matrix", &DBG_ScriptBox::m_mat)
		.def_readwrite("size", &DBG_ScriptBox::m_size),

		class_<DBG_ScriptLine, DBG_ScriptObject>("DBG_ScriptLine")
		.def_readwrite("point_a", &DBG_ScriptLine::m_point_a)
		.def_readwrite("point_b", &DBG_ScriptLine::m_point_b)
		];

	module(L, "debug_render")
		[
			def("add_object", add_object),
			def("remove_object", remove_object),
			def("get_object", get_object),
			def("get_flags", get_flags),
			def("set_flags", set_flags)
		];


	module(L, "level")
		[
			//Alundaio: Extend level namespace exports
#ifdef NAMESPACE_LEVEL_EXPORTS
			def("send", &g_send), //allow the ability to send netpacket to level
			def("get_target_obj", &g_get_target_obj), //intentionally named to what is in xray extensions
			def("get_target_dist", &g_get_target_dist),
			def("get_target_element", &g_get_target_element), //Can get bone cursor is targetting
			
			// demonized: get world position under crosshair
			def("get_target_pos", &g_get_target_pos),

			// demonized: get result of crosshair ray query
			def("get_target_result", &g_get_target_result),

			def("spawn_item", &spawn_section),
			def("get_active_cam", &get_active_cam),
			def("set_active_cam", &set_active_cam),
			def("get_start_time", &get_start_time),
			def("get_view_entity", &get_view_entity_script),
			def("set_view_entity", &set_view_entity_script),
#endif
			//Alundaio: END
			// obsolete\deprecated
			// demonized: add u16 override for better performance
			def("object_by_id", ((CScriptGameObject * (*)(u16)) & get_object_by_id)),
			def("object_by_id", ((CScriptGameObject* (*)()) & get_object_by_id)),
			def("object_by_id", ((CScriptGameObject* (*)(const luabind::object&)) & get_object_by_id)),
#ifdef DEBUG
		def("debug_object",						get_object_by_name),
		def("debug_actor",						tpfGetActor),
		def("check_object",						check_object),
#endif

			def("get_weather", get_weather),
			def("set_weather", set_weather),
			def("set_weather_fx", set_weather_fx),
			def("start_weather_fx_from_time", start_weather_fx_from_time),
			def("is_wfx_playing", is_wfx_playing),
			def("get_wfx_time", get_wfx_time),
			def("stop_weather_fx", stop_weather_fx),

			def("environment", environment),

			def("set_time_factor", set_time_factor),
			def("get_time_factor", get_time_factor),

			def("set_game_difficulty", set_game_difficulty),
			def("get_game_difficulty", get_game_difficulty),

			def("get_time_days", get_time_days),
			def("get_time_hours", get_time_hours),
			def("get_time_minutes", get_time_minutes),
			def("change_game_time", change_game_time),

			def("high_cover_in_direction", high_cover_in_direction),
			def("low_cover_in_direction", low_cover_in_direction),
			def("vertex_in_direction", vertex_in_direction),
			def("rain_factor", rain_factor),
			def("rain_wetness", rain_wetness),
			def("rain_hemi", rain_hemi),
			def("patrol_path_exists", patrol_path_exists),
			def("vertex_position", vertex_position),
			def("name", get_name),
			def("prefetch_sound", prefetch_sound),

			def("client_spawn_manager", get_client_spawn_manager),

			def("map_add_object_spot_ser", map_add_object_spot_ser),
			def("map_add_object_spot", map_add_object_spot),
			//-		def("map_add_object_spot_complex",		map_add_object_spot_complex),
			def("map_remove_object_spot", map_remove_object_spot),
			def("map_has_object_spot", map_has_object_spot),
			def("map_change_spot_hint", map_change_spot_hint),

			// demonized: remove all map object spots by id
			def("map_remove_all_object_spots", map_remove_all_object_spots),
			def("map_get_object_spot_static", map_get_spot_static),
			def("map_get_object_minimap_spot_static", map_get_minimap_spot_static),

			def("add_dialog_to_render", add_dialog_to_render),
			def("remove_dialog_to_render", remove_dialog_to_render),
			def("hide_indicators", hide_indicators),
			def("hide_indicators_safe", hide_indicators_safe),

			def("show_indicators", show_indicators),
			def("show_weapon", show_weapon),
			def("add_call", ((void (*)(const luabind::functor<bool>&, const luabind::functor<void>&))&add_call)),
			def("add_call", ((void (*)(const luabind::object&, const luabind::functor<bool>&,
			                           const luabind::functor<void>&))&add_call)),
			def("add_call", ((void (*)(const luabind::object&, LPCSTR, LPCSTR))&add_call)),
			def("remove_call", ((void (*)(const luabind::functor<bool>&, const luabind::functor<void>&))&remove_call)),
			def("remove_call",
			    ((void (*)(const luabind::object&, const luabind::functor<bool>&, const luabind::functor<void>&))&
				    remove_call)),
			def("remove_call", ((void (*)(const luabind::object&, LPCSTR, LPCSTR))&remove_call)),
			def("remove_calls_for_object", remove_calls_for_object),
			def("present", is_level_present),
			def("disable_input", disable_input),
			def("enable_input", enable_input),
			def("spawn_phantom", spawn_phantom),

			def("get_bounding_volume", get_bounding_volume),

			def("iterate_sounds", &iterate_sounds1),
			def("iterate_sounds", &iterate_sounds2),
			def("physics_world", &physics_world_scripted),
			def("get_snd_volume", &get_snd_volume),
			def("get_rain_volume", &get_rain_volume),
			def("set_snd_volume", &set_snd_volume),
			def("get_music_volume", &get_music_volume),
			def("set_music_volume", &set_music_volume),
			def("add_cam_effector", ((float (*)(LPCSTR, int, bool, LPCSTR))&add_cam_effector)),
			def("add_cam_effector", ((float (*)(LPCSTR, int, bool, LPCSTR, float))&add_cam_effector)),
			def("add_cam_effector", ((float (*)(LPCSTR, int, bool, LPCSTR, float, bool))&add_cam_effector)),
			def("add_cam_effector", ((float (*)(LPCSTR, int, bool, LPCSTR, float, bool, float))&add_cam_effector)),

			// demonized: Set custom camera position and direction with movement smoothing (for cutscenes, etc)
			def("set_cam_custom_position_direction", ((void (*)(Fvector&, Fvector&, unsigned int, bool, bool))& set_cam_position_direction)),
			def("set_cam_custom_position_direction", ((void (*)(Fvector&, Fvector&, unsigned int, bool))&set_cam_position_direction)),
			def("set_cam_custom_position_direction", ((void (*)(Fvector&, Fvector&, unsigned int))&set_cam_position_direction)),
			def("set_cam_custom_position_direction", ((void (*)(Fvector&, Fvector&))&set_cam_position_direction)),
			def("remove_cam_custom_position_direction", &remove_cam_position_direction),

			def("remove_cam_effector", &remove_cam_effector),
			def("set_cam_effector_factor", &set_cam_effector_factor),
			def("get_cam_effector_factor", &get_cam_effector_factor),
			def("get_cam_effector_length", &get_cam_effector_length),
			def("check_cam_effector", &check_cam_effector),
			def("add_pp_effector", &add_pp_effector),
			def("set_pp_effector_factor", &set_pp_effector_factor),
			def("set_pp_effector_factor", &set_pp_effector_factor2),
			def("remove_pp_effector", &remove_pp_effector),

			def("add_complex_effector", &add_complex_effector),
			def("remove_complex_effector", &remove_complex_effector),

			def("vertex_id", &vertex_id),

			def("game_id", &GameID),
			def("ray_pick", &ray_pick),

			def("press_action", &LevelPressAction),
			def("release_action", &LevelReleaseAction),
			def("hold_action", &LevelHoldAction),

			def("actor_moving_state", &ActorMovingState),
			def("get_env_rads", &get_env_rads),
			def("iterate_nearest", &iterate_nearest),
			def("pick_material", &PickMaterial)
		],

		module(L, "actor_stats")
		[
			def("add_points", &add_actor_points),
			def("add_points_str", &add_actor_points_str),
			def("get_points", &get_actor_points)
		];
	module(L)
	[
		class_<CRayPick>("ray_pick")
		.def(constructor<>())
		.def(constructor<Fvector&, Fvector&, float, collide::rq_target, CScriptGameObject*>())
		.def("set_position", &CRayPick::set_position)
		.def("set_direction", &CRayPick::set_direction)
		.def("set_range", &CRayPick::set_range)
		.def("set_flags", &CRayPick::set_flags)
		.def("set_ignore_object", &CRayPick::set_ignore_object)
		.def("query", &CRayPick::query)
		.def("get_result", &CRayPick::get_result)
		.def("get_object", &CRayPick::get_object)
		.def("get_distance", &CRayPick::get_distance)
		.def("get_element", &CRayPick::get_element),
		class_<script_rq_result>("rq_result")
		.def_readonly("object", &script_rq_result::O)
		.def_readonly("range", &script_rq_result::range)
		.def_readonly("element", &script_rq_result::element)
		.def_readonly("material_name", &script_rq_result::pMaterialName)
		.def_readonly("material_flags", &script_rq_result::pMaterialFlags)
		.def_readonly("material_phfriction", &script_rq_result::fPHFriction)
		.def_readonly("material_phdamping", &script_rq_result::fPHDamping)
		.def_readonly("material_phspring", &script_rq_result::fPHSpring)
		.def_readonly("material_phbounce_start_velocity", &script_rq_result::fPHBounceStartVelocity)
		.def_readonly("material_phbouncing", &script_rq_result::fPHBouncing)
		.def_readonly("material_flotation_factor", &script_rq_result::fFlotationFactor)
		.def_readonly("material_shoot_factor", &script_rq_result::fShootFactor)
		.def_readonly("material_shoot_factor_mp", &script_rq_result::fShootFactorMP)
		.def_readonly("material_bounce_damage_factor", &script_rq_result::fBounceDamageFactor)
		.def_readonly("material_injurious_speed", &script_rq_result::fInjuriousSpeed)
		.def_readonly("material_vis_transparency_factor", &script_rq_result::fVisTransparencyFactor)
		.def_readonly("material_snd_occlusion_factor", &script_rq_result::fSndOcclusionFactor)
		.def_readonly("material_density_factor", &script_rq_result::fDensityFactor)
		.def(constructor<>()),
		class_<enum_exporter<collide::rq_target>>("rq_target")
		.enum_("targets")
		[
			value("rqtNone", int(collide::rqtNone)),
			value("rqtObject", int(collide::rqtObject)),
			value("rqtStatic", int(collide::rqtStatic)),
			value("rqtShape", int(collide::rqtShape)),
			value("rqtObstacle", int(collide::rqtObstacle)),
			value("rqtBoth", int(collide::rqtBoth)),
			value("rqtDyn", int(collide::rqtDyn))
		]
	];

	module(L)
	[
		def("command_line", &command_line),
		def("IsGameTypeSingle", &IsGameTypeSingle),
		def("IsDynamicMusic", &IsDynamicMusic),
		def("render_get_dx_level", &render_get_dx_level),
		def("IsImportantSave", &IsImportantSave)
	];

	module(L, "weather")
	[
		def("get_value_numric", get_weather_value_numric),
		def("get_value_vector", get_weather_value_vector),
		def("get_value_string", get_weather_value_string),
		def("pause", pause_weather),
		def("is_paused",is_weather_paused),
		def("set_value_numric", set_weather_value_numric),
		def("set_value_vector", set_weather_value_vector),
		def("set_value_string", set_weather_value_string),
		def("reload", reload_weather),
		def("boost_value", boost_weather_value),
		def("boost_reset", boost_weather_reset),
		def("sun_time", sun_time)
	];

	module(L, "hud_adjust")
		[
		def("enabled", hud_adj_state),
		def("set_vector", hud_adj_offs),
		def("set_value", hud_adj_value),
		def("remove_hud_model", remove_hud_model)
	];

	module(L, "relation_registry")
	[
		def("community_goodwill", &g_community_goodwill),
		def("set_community_goodwill", &g_set_community_goodwill),
		def("change_community_goodwill", &g_change_community_goodwill),

		def("community_relation", &g_get_community_relation),
		def("set_community_relation", &g_set_community_relation),
		def("get_general_goodwill_between", &g_get_general_goodwill_between)
	];
	module(L, "game")
	[
		class_<xrTime>("CTime")
		.enum_("date_format")
		[
			value("DateToDay", int(InventoryUtilities::edpDateToDay)),
			value("DateToMonth", int(InventoryUtilities::edpDateToMonth)),
			value("DateToYear", int(InventoryUtilities::edpDateToYear))
		]
		.enum_("time_format")
		[
			value("TimeToHours", int(InventoryUtilities::etpTimeToHours)),
			value("TimeToMinutes", int(InventoryUtilities::etpTimeToMinutes)),
			value("TimeToSeconds", int(InventoryUtilities::etpTimeToSeconds)),
			value("TimeToMilisecs", int(InventoryUtilities::etpTimeToMilisecs))
		]
		.def(constructor<>())
		.def(constructor<const xrTime&>())
		.def(const_self < xrTime())
		.def(const_self <= xrTime())
		.def(const_self > xrTime())
		.def(const_self >= xrTime())
		.def(const_self == xrTime())
		.def(self + xrTime())
		.def(self - xrTime())

		.def("diffSec", &xrTime::diffSec_script)
		.def("add", &xrTime::add_script)
		.def("sub", &xrTime::sub_script)

		.def("setHMS", &xrTime::setHMS)
		.def("setHMSms", &xrTime::setHMSms)
		.def("set", &xrTime::set)
		.def("get", &xrTime::get,
		     out_value(_2) + out_value(_3) + out_value(_4) + out_value(_5) + out_value(_6) + out_value(_7) +
		     out_value(_8))
		.def("dateToString", &xrTime::dateToString)
		.def("timeToString", &xrTime::timeToString),
		// declarations
		def("time", get_time),
		def("get_game_time", get_time_struct),
		//		def("get_surge_time",	Game::get_surge_time),
		//		def("get_object_by_name",Game::get_object_by_name),

		def("start_tutorial", &start_tutorial),
		def("stop_tutorial", &stop_tutorial),
		def("has_active_tutorial", &has_active_tutotial),
		def("translate_string", &translate_string),
		def("reload_language", &reload_language),
		def("get_resolutions", &vid_modes_string),
		def("play_hud_motion", PlayHudMotion),
		def("stop_hud_motion", StopHudMotion),
		def("get_motion_length", MotionLength),
		def("hud_motion_allowed", AllowHudMotion),
		def("play_hud_anm", PlayBlendAnm),
		def("stop_hud_anm", StopBlendAnm),
		def("stop_all_hud_anms", StopAllBlendAnms),
		def("set_hud_anm_time", SetBlendAnmTime),
		def("only_allow_movekeys", block_all_except_movement),
		def("only_movekeys_allowed", only_movement_allowed),
		def("set_actor_allow_ladder", set_actor_allow_ladder),
		def("set_nv_lumfactor", set_nv_lumfactor),
		def("reload_ui_xml", reload_hud_xml),
		def("actor_weapon_lowered", actor_safemode),
		def("actor_lower_weapon", actor_set_safemode),
		def("prefetch_texture", prefetch_texture),
		def("prefetch_model", prefetch_model),
		def("get_visual_userdata", GetVisualUserdata),
		def("world2ui", world2ui),
		def("ui2world", (void (*)(Fvector2, Fvector&, u16&))&ui2world, pure_out_value(_2) + pure_out_value(_3)),
		def("ui2world", (void (*)(Fvector&, Fvector&, u16&))&ui2world, pure_out_value(_2) + pure_out_value(_3)),
		
		// demonized: adjust game news time
		def("change_game_news_show_time", &change_game_news_show_time),
		def("update_pda_news_from_uiwindow", &update_pda_news_from_uiwindow)
	];
}
