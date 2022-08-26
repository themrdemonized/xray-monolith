#include "stdafx.h"
#include "specific_character.h"

#ifdef  XRGAME_EXPORTS
#include "PhraseDialog.h"
#include "string_table.h"

#include "ai_space.h"
#include "Script_Game_Object.h"

SSpecificCharacterData::SSpecificCharacterData()
{
	m_sGameName.clear();
	m_sBioText = NULL;
	m_sVisual.clear();
	m_sSupplySpawn.clear();
	m_sNpcConfigSect.clear();


	m_StartDialog = NULL;
	m_ActorDialogs.clear();

	m_Rank = NO_RANK;
	m_Reputation = NO_REPUTATION;

	money_def.inf_money = false;
	money_def.max_money = 0;
	money_def.min_money = 0;

	rank_def.min = NO_RANK;
	rank_def.max = NO_RANK;

	reputation_def.min = NO_REPUTATION;
	reputation_def.max = NO_REPUTATION;

	m_bNoRandom = false;
	m_bDefaultForCommunity = false;
	m_fPanic_threshold = 0.0f;
	m_fHitProbabilityFactor = 1.f;
	m_crouch_type = 0;
	m_upgrade_mechanic = false;
}

SSpecificCharacterData::~SSpecificCharacterData()
{
}

#endif

CSpecificCharacter::CSpecificCharacter()
{
	m_OwnId = NULL;
}


CSpecificCharacter::~CSpecificCharacter()
{
}


void CSpecificCharacter::InitXmlIdToIndex()
{
	if (!id_to_index::tag_name)
		id_to_index::tag_name = "specific_character";
	if (!id_to_index::file_str)
		id_to_index::file_str = pSettings->r_string("profiles", "specific_characters_files");
}


void CSpecificCharacter::Load(shared_str id)
{
	R_ASSERT(id.size());
	m_OwnId = id;
	inherited_shared::load_shared(m_OwnId, NULL);
}


void CSpecificCharacter::load_shared(LPCSTR)
{
#if 0
	CTimer			timer;
	timer.Start();
#endif
	const ITEM_DATA& item_data = *id_to_index::GetById(m_OwnId);

	CUIXml* pXML = item_data._xml;

	pXML->SetLocalRoot(pXML->GetRoot());


	XML_NODE* item_node = pXML->NavigateToNode(id_to_index::tag_name, item_data.pos_in_file);
	R_ASSERT3(item_node, "specific_character id=", *item_data.id);

	pXML->SetLocalRoot(item_node);


	int norandom = pXML->ReadAttribInt(item_node, "no_random", 0);
	if (1 == norandom)
		data()->m_bNoRandom = true;
	else
		data()->m_bNoRandom = false;

	int team_default = pXML->ReadAttribInt(item_node, "team_default", 0);
	if (1 == team_default)
		data()->m_bDefaultForCommunity = true;
	else
		data()->m_bDefaultForCommunity = false;

	R_ASSERT3(!(data()->m_bNoRandom && data()->m_bDefaultForCommunity),
	          "cannot set 'no_random' and 'team_default' flags simultaneously, profile id", *shared_str(item_data.id));

#ifdef  XRGAME_EXPORTS

	LPCSTR start_dialog = pXML->Read("start_dialog", 0, NULL);
	if (start_dialog)
	{
		data()->m_StartDialog = start_dialog;
	}
	else
		data()->m_StartDialog = NULL;

	int dialogs_num = pXML->GetNodesNum(pXML->GetLocalRoot(), "actor_dialog");
	data()->m_ActorDialogs.clear();
	for (int i = 0; i < dialogs_num; ++i)
	{
		shared_str dialog_name = pXML->Read(pXML->GetLocalRoot(), "actor_dialog", i, "");
		data()->m_ActorDialogs.push_back(dialog_name);
	}

	luabind::functor<luabind::object> funct;
	if (ai().script_engine().functor("_G.CSpecificCharacterDialogList", funct))
	{
		luabind::object table = luabind::newtable(ai().script_engine().lua());
		int i = 1;
		for (auto const &dialog : data()->m_ActorDialogs) {
			table[i] = dialog.c_str();
			i++;
		}
		auto character_name = item_data.id.c_str();
		luabind::object output = funct(character_name, table);
		if (output && output.type() == LUA_TTABLE) {
			data()->m_ActorDialogs.clear();
			luabind::object::iterator i = output.begin();
			luabind::object::iterator e = output.end();
			for (; i != e; ++i) {
				luabind::object v = *i;
				if (v.type() == LUA_TSTRING) {
					shared_str dialog_name = luabind::object_cast<LPCSTR>(v);
					//Msg("character_id %s, dialog_name %s", character_name, dialog_name.c_str());
					data()->m_ActorDialogs.push_back(dialog_name);
				}
			}
		}
	}

	data()->m_icon_name = pXML->Read("icon", 0, "ui_npc_u_barman");


	//игровое имя персонажа
	data()->m_sGameName = pXML->Read("name", 0, "");
	data()->m_sBioText = CStringTable().translate(pXML->Read("bio", 0, ""));


	data()->m_fPanic_threshold = pXML->ReadFlt("panic_threshold", 0, 0.f);
	data()->m_fHitProbabilityFactor = pXML->ReadFlt("hit_probability_factor", 0, 1.f);
	data()->m_crouch_type = pXML->ReadInt("crouch_type", 0, 0);
	data()->m_upgrade_mechanic = (pXML->ReadInt("mechanic_mode", 0, 0) == 1);

	data()->m_critical_wound_weights = pXML->Read("critical_wound_weights", 0, "1");

#endif

	data()->m_sVisual = pXML->Read("visual", 0, "");


#ifdef  XRGAME_EXPORTS
	data()->m_sSupplySpawn = pXML->Read("supplies", 0, "");

	if (!data()->m_sSupplySpawn.empty())
	{
		xr_string& str = data()->m_sSupplySpawn;
		xr_string::size_type pos = str.find("\\n");

		while (xr_string::npos != pos)
		{
			str.replace(pos, 2, "\n");
			pos = str.find("\\n", pos + 1);
		}
	}

	data()->m_sNpcConfigSect = pXML->Read("npc_config", 0, "");
	data()->m_sound_voice_prefix = pXML->Read("snd_config", 0, "");

	data()->m_terrain_sect = pXML->Read("terrain_sect", 0, "");

#endif

	data()->m_Classes.clear();
	int classes_num = pXML->GetNodesNum(pXML->GetLocalRoot(), "class");
	for (int i = 0; i < classes_num; i++)
	{
		LPCSTR char_class = pXML->Read("class", 0, "");
		if (char_class)
		{
			char* buf_str = xr_strdup(char_class);
			xr_strlwr(buf_str);
			data()->m_Classes.push_back(buf_str);
			xr_free(buf_str);
		}
	}


#ifdef  XRGAME_EXPORTS

	LPCSTR team = pXML->Read("community", 0, NULL);
	R_ASSERT3(team != NULL, "'community' field not fulfiled for specific character", *m_OwnId);

	char* buf_str = xr_strdup(team);
	xr_strlwr(buf_str);
	data()->m_Community.set(buf_str);
	xr_free(buf_str);

	if (data()->m_Community.index() == NO_COMMUNITY_INDEX)
		Debug.fatal(DEBUG_INFO, "wrong 'community' '%s' in specific character %s ", team, *m_OwnId);


	int min_rank = pXML->ReadAttribInt("rank", 0, "min", NO_RANK);
	int max_rank = pXML->ReadAttribInt("rank", 0, "max", NO_RANK);
	if (min_rank != NO_RANK && max_rank != NO_RANK)
	{
		RankDef().min = _min(min_rank, max_rank);
		RankDef().max = _max(max_rank, min_rank);
	}
	else
	{
		int rank = pXML->ReadInt("rank", 0, NO_RANK);
		R_ASSERT3(rank != NO_RANK, "'rank' field not fulfiled for specific character", *m_OwnId);
		RankDef().min = rank;
		RankDef().max = rank;
	}

	int min_reputation = pXML->ReadAttribInt("reputation", 0, "min", NO_REPUTATION);
	int max_reputation = pXML->ReadAttribInt("reputation", 0, "max", NO_REPUTATION);
	if (min_reputation != NO_REPUTATION && max_reputation != NO_REPUTATION)
	{
		ReputationDef().min = _min(min_reputation, max_reputation);
		ReputationDef().max = _max(max_reputation, min_reputation);
	}
	else
	{
		int rep = pXML->ReadInt("reputation", 0, NO_REPUTATION);
		R_ASSERT3(rep != NO_REPUTATION, "'reputation' field not fulfiled for specific character", *m_OwnId);
		ReputationDef().min = rep;
		ReputationDef().max = rep;
	}

	if (pXML->NavigateToNode(pXML->GetLocalRoot(), "money", 0))
	{
		MoneyDef().min_money = pXML->ReadAttribInt("money", 0, "min");
		MoneyDef().max_money = pXML->ReadAttribInt("money", 0, "max");
		MoneyDef().inf_money = !!pXML->ReadAttribInt("money", 0, "infinitive");
		MoneyDef().max_money = _max(MoneyDef().max_money, MoneyDef().min_money); // :)
	}
	else
	{
		MoneyDef().min_money = 0;
		MoneyDef().max_money = 0;
		MoneyDef().inf_money = false;
	}

	luabind::functor<luabind::object> init_funct;
	if (ai().script_engine().functor("_G.CSpecificCharacterInit", init_funct))
	{
		luabind::object table = luabind::newtable(ai().script_engine().lua());
		table["name"] = Name();
		table["bio"] = Bio().c_str();
		table["community"] = Community().id().c_str();
		table["icon"] = data()->m_icon_name.c_str();
		table["start_dialog"] = data()->m_StartDialog.c_str();
		table["panic_threshold"] = panic_threshold();
		table["hit_probability_factor"] = hit_probability_factor();
		table["crouch_type"] = crouch_type();
		table["mechanic_mode"] = upgrade_mechanic();
		table["critical_wound_weights"] = critical_wound_weights();
		table["supplies"] = SupplySpawn();
		table["visual"] = Visual();
		table["npc_config"] = NpcConfigSect();
		table["snd_config"] = sound_voice_prefix();
		table["terrain_sect"] = terrain_sect().c_str();
		table["rank_min"] = RankDef().min;
		table["rank_max"] = RankDef().max;
		table["reputation_min"] = ReputationDef().min;
		table["reputation_max"] = ReputationDef().max;
		table["money_min"] = MoneyDef().min_money;
		table["money_max"] = MoneyDef().max_money;
		table["money_infinitive"] = MoneyDef().inf_money;
		auto character_name = item_data.id.c_str();
		luabind::object output = init_funct(character_name, table);
		if (output && output.type() == LUA_TTABLE) {
			data()->m_sGameName = luabind::object_cast<LPCSTR>(output["name"]);
			data()->m_sBioText = CStringTable().translate(luabind::object_cast<LPCSTR>(output["bio"]));

			data()->m_Community.set(luabind::object_cast<LPCSTR>(output["community"]));
			if (data()->m_Community.index() == NO_COMMUNITY_INDEX)
				Debug.fatal(DEBUG_INFO, "wrong 'community' '%s' in specific character %s ", luabind::object_cast<LPCSTR>(output["community"]), *m_OwnId);

			data()->m_icon_name = luabind::object_cast<LPCSTR>(output["icon"]);
			data()->m_StartDialog = output["start_dialog"].type() == LUA_TSTRING ? luabind::object_cast<LPCSTR>(output["start_dialog"]) : NULL;
			data()->m_fPanic_threshold = luabind::object_cast<float>(output["panic_threshold"]);
			data()->m_fHitProbabilityFactor = luabind::object_cast<float>(output["hit_probability_factor"]);
			data()->m_crouch_type = luabind::object_cast<int>(output["crouch_type"]);
			data()->m_upgrade_mechanic = luabind::object_cast<bool>(output["mechanic_mode"]);
			data()->m_critical_wound_weights = luabind::object_cast<LPCSTR>(output["critical_wound_weights"]);
			data()->m_sVisual = luabind::object_cast<LPCSTR>(output["visual"]);
			data()->m_sNpcConfigSect = luabind::object_cast<LPCSTR>(output["npc_config"]);
			data()->m_sound_voice_prefix = luabind::object_cast<LPCSTR>(output["snd_config"]);
			data()->m_terrain_sect = luabind::object_cast<LPCSTR>(output["terrain_sect"]);

			data()->m_sSupplySpawn = luabind::object_cast<LPCSTR>(output["supplies"]);
			if (!data()->m_sSupplySpawn.empty())
			{
				xr_string& str = data()->m_sSupplySpawn;
				xr_string::size_type pos = str.find("\\n");
				while (xr_string::npos != pos)
				{
					str.replace(pos, 2, "\n");
					pos = str.find("\\n", pos + 1);
				}
			}

			RankDef().min = _min(luabind::object_cast<int>(output["rank_min"]), luabind::object_cast<int>(output["rank_max"]));
			RankDef().max = _max(luabind::object_cast<int>(output["rank_min"]), luabind::object_cast<int>(output["rank_max"]));

			ReputationDef().min = _min(luabind::object_cast<int>(output["reputation_min"]), luabind::object_cast<int>(output["reputation_max"]));
			ReputationDef().max = _max(luabind::object_cast<int>(output["reputation_min"]), luabind::object_cast<int>(output["reputation_max"]));

			MoneyDef().min_money = _min(luabind::object_cast<int>(output["money_min"]), luabind::object_cast<int>(output["money_max"]));
			MoneyDef().max_money = _max(luabind::object_cast<int>(output["money_min"]), luabind::object_cast<int>(output["money_max"]));
			MoneyDef().inf_money = luabind::object_cast<bool>(output["money_infinitive"]);
		}
	}

#endif

#if 0
	Msg("CSpecificCharacter::load_shared() takes %f milliseconds", timer.GetElapsed_sec()*1000.f);
#endif
}


#ifdef  XRGAME_EXPORTS

LPCSTR CSpecificCharacter::Name() const
{
	return data()->m_sGameName.c_str();
}

shared_str CSpecificCharacter::Bio() const
{
	return data()->m_sBioText;
}

const CHARACTER_COMMUNITY& CSpecificCharacter::Community() const
{
	return data()->m_Community;
}

LPCSTR CSpecificCharacter::SupplySpawn() const
{
	return data()->m_sSupplySpawn.c_str();
}

LPCSTR CSpecificCharacter::NpcConfigSect() const
{
	return data()->m_sNpcConfigSect.c_str();
}

LPCSTR CSpecificCharacter::sound_voice_prefix() const
{
	return data()->m_sound_voice_prefix.c_str();
}

float CSpecificCharacter::panic_threshold() const
{
	return data()->m_fPanic_threshold;
}

float CSpecificCharacter::hit_probability_factor() const
{
	return data()->m_fHitProbabilityFactor;
}

int CSpecificCharacter::crouch_type() const
{
	return data()->m_crouch_type;
}

bool CSpecificCharacter::upgrade_mechanic() const
{
	return data()->m_upgrade_mechanic;
}

LPCSTR CSpecificCharacter::critical_wound_weights() const
{
	return data()->m_critical_wound_weights.c_str();
}

#endif

shared_str CSpecificCharacter::terrain_sect() const
{
	return data()->m_terrain_sect;
}

CHARACTER_RANK_VALUE CSpecificCharacter::Rank() const
{
	return data()->m_Rank;
}

CHARACTER_REPUTATION_VALUE CSpecificCharacter::Reputation() const
{
	return data()->m_Reputation;
}

LPCSTR CSpecificCharacter::Visual() const
{
	return data()->m_sVisual.c_str();
}
