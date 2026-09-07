////////////////////////////////////////////////////////////////////////////
//	Module 		: script_sound.cpp
//	Created 	: 06.02.2004
//  Modified 	: 06.02.2004
//	Author		: Dmitriy Iassenev
//	Description : XRay Script sound class
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "script_sound.h"
#include "script_game_object.h"
#include "gameobject.h"
#include "ai_space.h"
#include "script_engine.h"

// { volume_mult = number } from a sound hook's return. 1.0 (vanilla) when nil, not a table, or not a number.
static float ambient_hook_volume_mult(const ::luabind::object& output)
{
	if (output && output.type() == LUA_TTABLE)
	{
		auto volume_mult_obj = output["volume_mult"];
		if (volume_mult_obj.type() == LUA_TNUMBER)
			return ::luabind::object_cast<float>(volume_mult_obj);
	}
	return 1.0f;
}

// COnBeforePlayScriptSound(file, pos, obj) -> { volume_mult }, the COnBeforePlayHudSound shape.
// 0 vetoes the play, <1 attenuates, nil = vanilla. Absent args reach Lua as nil. An unset global costs one lookup.
static float script_sound_hook_volume_mult(LPCSTR file, const Fvector* pos, CScriptGameObject* object)
{
	::luabind::functor<::luabind::object> funct;
	if (!ai().script_engine().functor("_G.COnBeforePlayScriptSound", funct))
		return 1.0f;
	Fvector obj_pos;                       // Play() carries no position: read the object's, but only past the gate
	if (!pos && object)
	{
		obj_pos = object->object().Position();
		pos = &obj_pos;
	}
	if (pos && object)
		return ambient_hook_volume_mult(funct(file, *pos, object));
	if (pos)
		return ambient_hook_volume_mult(funct(file, *pos));
	return ambient_hook_volume_mult(funct(file));
}

CScriptSound::CScriptSound(LPCSTR caSoundName, ESoundTypes sound_type)
{
	m_caSoundToPlay = caSoundName;
	string_path l_caFileName;
	VERIFY(::Sound) ;
	if (FS.exist(l_caFileName, "$game_sounds$", caSoundName, ".ogg"))
		m_sound.create(caSoundName, st_Effect, sound_type);
	else
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "File not found \"%s\"!", l_caFileName);
		m_sound.create("$no_sound.ogg", st_Effect, sound_type);
	}
}

CScriptSound::~CScriptSound()
{
#ifdef DEBUG
	THROW3(!m_sound._feedback(), "playing sound is not completed, but is destroying",
	       m_sound._handle() ? m_sound._handle()->file_name() : "unknown");
#endif
	m_sound.destroy();
}

Fvector CScriptSound::GetPosition() const
{
	VERIFY(m_sound._handle());
	const CSound_params* l_tpSoundParams = m_sound.get_params();
	if (l_tpSoundParams)
		return (l_tpSoundParams->position);
	else
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
		                                "Sound was not launched, can't get position!");
		return (Fvector().set(0, 0, 0));
	}
}

void CScriptSound::Play(CScriptGameObject* object, float delay, int flags)
{
	THROW3(m_sound._handle(), "There is no sound", *m_caSoundToPlay);
	//	Msg							("%6d : CScriptSound::Play (%s), delay %f, flags %d",Device.dwTimeGlobal,m_sound._handle()->file_name(),delay,flags);
	float volume_mult = script_sound_hook_volume_mult(*m_caSoundToPlay, NULL, object);
	if (volume_mult <= EPS_S)
		return;
	m_sound.play((object) ? &object->object() : NULL, flags, delay);
	if (volume_mult < 1.0f)
		m_sound.set_volume(volume_mult);
}

void CScriptSound::PlayAtPos(CScriptGameObject* object, const Fvector& position, float delay, int flags)
{
	THROW3(m_sound._handle(), "There is no sound", *m_caSoundToPlay);
	//	Msg							("%6d : CScriptSound::Play (%s), delay %f, flags %d",m_sound._handle()->file_name(),delay,flags);
	float volume_mult = script_sound_hook_volume_mult(*m_caSoundToPlay, &position, object);
	if (volume_mult <= EPS_S)
		return;
	m_sound.play_at_pos((object) ? &object->object() : NULL, position, flags, delay);
	if (volume_mult < 1.0f)
		m_sound.set_volume(volume_mult);
}

void CScriptSound::PlayNoFeedback(CScriptGameObject* object, u32 flags/*!< Looping */, float delay/*!< Delay */,
                                  Fvector pos, float vol, float freq)
{
	THROW3(m_sound._handle(), "There is no sound", *m_caSoundToPlay);
	float volume_mult = script_sound_hook_volume_mult(*m_caSoundToPlay, &pos, object);
	if (volume_mult <= EPS_S)
		return;
	vol *= volume_mult;
	m_sound.play_no_feedback((object) ? &object->object() : NULL, flags, delay, &pos, &vol, &freq);
}
