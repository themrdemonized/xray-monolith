////////////////////////////////////////////////////////////////////////////
//	Module 		: sound_memory_manager_inline.h
//	Created 	: 02.10.2001
//  Modified 	: 19.11.2003
//	Author		: Dmitriy Iassenev
//	Description : Sound memory manager inline functions
////////////////////////////////////////////////////////////////////////////

#pragma once

IC CSoundMemoryManager::CSoundMemoryManager(CCustomMonster* object, CAI_Stalker* stalker,
                                            CSound_UserDataVisitor* visitor)
{
	VERIFY(object);
	m_object = object;
	VERIFY(visitor);
	m_visitor = visitor;
	m_stalker = stalker;
	m_max_sound_count = 0;
#ifdef USE_SELECTED_SOUND
	m_selected_sound			= 0;
#endif
}

IC const CSoundMemoryManager::SOUNDS& CSoundMemoryManager::objects() const
{
	VERIFY(m_sounds);
	return (*m_sounds);
}

IC void CSoundMemoryManager::priority(const ESoundTypes& sound_type, u32 priority)
{
	PRIORITIES::const_iterator I = m_priorities.find(sound_type);
	VERIFY(m_priorities.end() == I);
	m_priorities.insert(std::make_pair(sound_type, priority));
}

#ifdef USE_SELECTED_SOUND
IC	const MemorySpace::CSoundObject *CSoundMemoryManager::sound		() const
{
	return						(m_selected_sound);
}
#endif

IC void CSoundMemoryManager::set_squad_objects(SOUNDS* squad_objects)
{
	m_sounds = squad_objects;
}

IC void CSoundMemoryManager::set_threshold(float threshold)
{
	m_sound_threshold = threshold;
	VERIFY(_valid(m_sound_threshold));
}

IC void CSoundMemoryManager::restore_threshold()
{
	m_sound_threshold = m_min_sound_threshold;
	VERIFY(_valid(m_sound_threshold));
}

IC void CSoundMemoryManager::set_min_threshold(float threshold)
{
	m_min_sound_threshold = threshold;
	m_sound_threshold = m_min_sound_threshold;
	VERIFY(_valid(m_sound_threshold));
}

IC void CSoundMemoryManager::set_factor(int sound_type, float factor)
{
	if ((sound_type & SOUND_TYPE_WEAPON) == SOUND_TYPE_WEAPON) m_weapon_factor = factor;
	if ((sound_type & SOUND_TYPE_ITEM) == SOUND_TYPE_ITEM) m_item_factor = factor;
	if ((sound_type & SOUND_TYPE_MONSTER) == SOUND_TYPE_MONSTER) m_npc_factor = factor;
	if ((sound_type & SOUND_TYPE_ANOMALY) == SOUND_TYPE_ANOMALY) m_anomaly_factor = factor;
	if ((sound_type & SOUND_TYPE_WORLD) == SOUND_TYPE_WORLD) m_world_factor = factor;
}

IC float CSoundMemoryManager::threshold() const
{
	return m_sound_threshold;
}
