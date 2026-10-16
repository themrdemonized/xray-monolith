////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_registry_container.h
//	Created 	: 01.07.2004
//  Modified 	: 01.07.2004
//	Author		: Dmitriy Iassenev
//	Description : ALife registry container class
////////////////////////////////////////////////////////////////////////////

#pragma once
#include "alife_abstract_registry.h"

#include "InfoPortionDefs.h"
#include "PdaMsg.h"
#include "encyclopedia_article_defs.h"
#include "alife_abstract_registry.h"
#include "character_info_defs.h"
#include "relation_registry_defs.h"


#include "GameTaskDefs.h"
#include "game_news.h"
#include "map_location_defs.h"

#include "actor_statistic_defs.h"
#include "alife_registry_version.h"
#include "../xrServerEntities/alife_space.h"

struct CInfoPortionRegistry : CALifeAbstractRegistry<ALife::_OBJECT_ID, KNOWN_INFO_VECTOR>
{
	virtual void load(IReader& stream)
	{
		load_object_ids(stream, alife_registry_load_version() >= ALIFE_VERSION_WIDE_IDS);
	}
};

struct CRelationRegistry : CALifeAbstractRegistry<ALife::_OBJECT_ID, RELATION_DATA>
{
	virtual void load(IReader& stream)
	{
		load_object_ids(stream, alife_registry_load_version() >= ALIFE_VERSION_WIDE_IDS);
	}
};

struct CGameNewsRegistry : CALifeAbstractRegistry<ALife::_OBJECT_ID, GAME_NEWS_VECTOR>
{
	virtual void load(IReader& stream)
	{
		load_object_ids(stream, alife_registry_load_version() >= ALIFE_VERSION_WIDE_IDS);
	}
};

using CSpecificCharacterRegistry = CALifeAbstractRegistry<shared_str, int>;

class CALifeRegistryContainer
{
public:
    CInfoPortionRegistry       m_info_portions;
    CRelationRegistry          m_character_relations;
    CGameNewsRegistry          m_game_news;
    CSpecificCharacterRegistry m_specific_characters;
    CMapLocationRegistry       m_map_locations;
    CGameTaskRegistry          m_game_tasks;
    CActorStatisticRegistry    m_actor_statistics;

    template <typename T>
    T& get();

    template <typename T>
    const T& get() const;

    void load(IReader& file_stream, u32 version);
    void save(IWriter& memory_stream);
};

#include "alife_registry_container_inline.h"
