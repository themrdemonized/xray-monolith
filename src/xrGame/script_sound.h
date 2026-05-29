////////////////////////////////////////////////////////////////////////////
//	Module 		: script_sound.h
//	Created 	: 06.02.2004
//  Modified 	: 06.02.2004
//	Author		: Dmitriy Iassenev
//	Description : XRay Script sound class
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "script_export_space.h"
#include "ai_sounds.h"

class CScriptGameObject;
class CSound_emitter;

class CScriptSound
{
	mutable ref_sound m_sound;
	shared_str m_caSoundToPlay;

	friend class CScriptSoundAction;
public:

	CScriptSound(LPCSTR caSoundName, ESoundTypes sound_type = SOUND_TYPE_NO_SOUND);
	virtual ~CScriptSound();
	IC u32 Length();
	IC void Play(CScriptGameObject* object);
	IC void Play(CScriptGameObject* object, float delay);
	void Play(CScriptGameObject* object, float delay, int flags);
	IC void PlayAtPos(CScriptGameObject* object, const Fvector& position);
	IC void PlayAtPos(CScriptGameObject* object, const Fvector& position, float delay);
	void PlayAtPos(CScriptGameObject* object, const Fvector& position, float delay, int flags);
	void PlayNoFeedback(CScriptGameObject* object, u32 flags/*!< Looping */, float delay/*!< Delay */, Fvector pos,
	                    float vol, float freq);
	IC void AttachTail(LPCSTR caSoundName);
	void Stop();
	IC void StopDeffered();
	IC void SetPosition(const Fvector& position);
	IC void SetFrequency(float frequency);
	IC void SetVolume(float volume);
	IC const CSound_params* GetParams();
	IC void SetParams(CSound_params* sound_params);
	void SetMinDistance(const float fMinDistance);
	IC void SetMaxDistance(const float fMaxDistance);
	Fvector GetPosition() const;
	IC const float GetFrequency() const;
	IC const float GetMinDistance() const;
	IC const float GetMaxDistance() const;
	IC const float GetVolume() const;
	IC bool IsPlaying() const;
	void set_persistent(bool bPersist);
	void set_persistent(bool bPersist, bool bPersistInMenu);
	bool is_persistent() const;
	bool is_persistent_in_menu() const;

private:
	void apply_pending_persistent();
	CSound_emitter* active_emitter(bool reconcile = true);
	bool m_bPersistentPending = false;
	bool m_bPersistentInMenuPending = true;

DECLARE_SCRIPT_REGISTER_FUNCTION
};

#include "script_sound_inline.h"
