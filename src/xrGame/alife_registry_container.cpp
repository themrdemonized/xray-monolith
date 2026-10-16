////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_registry_container.cpp
//	Created 	: 01.07.2004
//  Modified 	: 01.07.2004
//	Author		: Dmitriy Iassenev
//	Description : ALife registry container class
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "alife_registry_container.h"
#include "object_interfaces.h"
#include "alife_space.h"

static u32 g_alife_registry_load_version = ALIFE_VERSION_WIDE_IDS;

u32 alife_registry_load_version()
{
	return g_alife_registry_load_version;
}

void set_alife_registry_load_version(u32 version)
{
	g_alife_registry_load_version = version;
}

void CALifeRegistryContainer::load(IReader& file_stream, u32 version)
{
	set_alife_registry_load_version(version);
	R_ASSERT2(file_stream.find_chunk(REGISTRY_CHUNK_DATA), "Can't find chunk REGISTRY_CHUNK_DATA!");

	m_info_portions.load(file_stream);
	m_character_relations.load(file_stream);
	m_game_news.load(file_stream);
	m_specific_characters.load(file_stream);
	m_map_locations.load(file_stream);
	m_game_tasks.load(file_stream);
	m_actor_statistics.load(file_stream);
}

void CALifeRegistryContainer::save(IWriter& memory_stream)
{
	memory_stream.open_chunk(REGISTRY_CHUNK_DATA);

	m_info_portions.save(memory_stream);
	m_character_relations.save(memory_stream);
	m_game_news.save(memory_stream);
	m_specific_characters.save(memory_stream);
	m_map_locations.save(memory_stream);
	m_game_tasks.save(memory_stream);
	m_actor_statistics.save(memory_stream);

	memory_stream.close_chunk();
}
