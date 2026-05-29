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
#include "../xrSound/Sound.h"

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

void CScriptSound::apply_pending_persistent()
{
	CSound_emitter* emitter = active_emitter(false);
	if (!m_bPersistentPending || !emitter)
		return;
	emitter->set_persistent_in_menu(m_bPersistentInMenuPending);
	emitter->set_persistent(true);
	m_bPersistentPending = false;
}

CSound_emitter* CScriptSound::active_emitter(bool reconcile)
{
	if (m_sound._feedback())
		return m_sound._feedback();
	if (reconcile)
		m_sound.reconcile_feedback();
	return m_sound._feedback();
}

void CScriptSound::Play(CScriptGameObject* object, float delay, int flags)
{
	THROW3(m_sound._handle(), "There is no sound", *m_caSoundToPlay);
	m_sound.play((object) ? &object->object() : NULL, flags, delay);
	apply_pending_persistent();
}

void CScriptSound::PlayAtPos(CScriptGameObject* object, const Fvector& position, float delay, int flags)
{
	THROW3(m_sound._handle(), "There is no sound", *m_caSoundToPlay);
	m_sound.play_at_pos((object) ? &object->object() : NULL, position, flags, delay);
	apply_pending_persistent();
}

void CScriptSound::PlayNoFeedback(CScriptGameObject* object, u32 flags/*!< Looping */, float delay/*!< Delay */,
                                  Fvector pos, float vol, float freq)
{
	THROW3(m_sound._handle(), "There is no sound", *m_caSoundToPlay);
	m_sound.play_no_feedback((object) ? &object->object() : NULL, flags, delay, &pos, &vol, &freq);
	apply_pending_persistent();
}

void CScriptSound::set_persistent(bool bPersist)
{
	set_persistent(bPersist, m_bPersistentInMenuPending);
}

void CScriptSound::set_persistent(bool bPersist, bool bPersistInMenu)
{
	m_bPersistentInMenuPending = bPersistInMenu;
	CSound_emitter* emitter = active_emitter();
	if (!emitter)
	{
		m_bPersistentPending = bPersist;
		if (!bPersist && Sound && m_sound._p)
			Sound->stop_emitters_for_owner(m_sound._p._get());
		return;
	}
	emitter->set_persistent_in_menu(bPersistInMenu);
	emitter->set_persistent(bPersist);
	m_bPersistentPending = false;
}

void CScriptSound::Stop()
{
	m_sound.stop();
}

bool CScriptSound::is_persistent() const
{
	if (CSound_emitter* emitter = m_sound._feedback())
		return emitter->is_persistent();
	return m_bPersistentPending;
}

bool CScriptSound::is_persistent_in_menu() const
{
	if (CSound_emitter* emitter = m_sound._feedback())
		return emitter->is_persistent_in_menu();
	return m_bPersistentInMenuPending;
}
