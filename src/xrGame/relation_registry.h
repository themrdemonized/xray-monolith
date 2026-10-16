//////////////////////////////////////////////////////////////////////////
// relation_registry.h: реестр для хранения данных об отношении персонажа к 
//						другим персонажам
//////////////////////////////////////////////////////////////////////////

#pragma once

#include "character_info_defs.h"
#include "../xrServerEntities/alife_space.h"

class CRelationRegistryWrapper;

class CInventoryOwner;
class CEntityAlive;

//////////////////////////////////////////////////////////////////////////

#define GAME_RELATIONS_SECT "game_relations"
#define ACTIONS_POINTS_SECT "action_points"

//////////////////////////////////////////////////////////////////////////

struct RELATION_REGISTRY
{
public:
	RELATION_REGISTRY();
	virtual ~RELATION_REGISTRY();

public:

	template <typename T>
	ALife::ERelationType GetRelationBetween(T char1, T char2) const;

	template <typename T>
	ALife::ERelationType GetRelationType(T from, T to) const;
	template <typename T>
	void SetRelationType(T from, T to, ALife::ERelationType new_relation);

	//общее отношение  одного персонажа к другому, вычисленное по формуле
	//с учетом всех факторов - величина от 
	//-100< (крайне враждебное) до >100 (очень дрюжелюбное)

	template <typename T>
	CHARACTER_GOODWILL GetAttitude(T from, T to) const;

	//личное отношение (благосклонность) одного персонажа к другому - 
	//величина от -100< (крайне враждебное) до >100 (очень дрюжелюбное)
	CHARACTER_GOODWILL GetGoodwill(ALife::_OBJECT_ID from, ALife::_OBJECT_ID to) const;
	void SetGoodwill(ALife::_OBJECT_ID from, ALife::_OBJECT_ID to, CHARACTER_GOODWILL goodwill);
	void ForceSetGoodwill(ALife::_OBJECT_ID from, ALife::_OBJECT_ID to, CHARACTER_GOODWILL goodwill);
	void ChangeGoodwill(ALife::_OBJECT_ID from, ALife::_OBJECT_ID to, CHARACTER_GOODWILL delta_goodwill);

	//отношения группировки к персонажу (именно так, а не наоборот)
	//т.е. персонаж сам помнит, как к нему какая группировка отностися
	CHARACTER_GOODWILL GetCommunityGoodwill(CHARACTER_COMMUNITY_INDEX from_community, ALife::_OBJECT_ID to_character) const;
	void SetCommunityGoodwill(CHARACTER_COMMUNITY_INDEX from_community, ALife::_OBJECT_ID to_character, CHARACTER_GOODWILL goodwill);
	void ChangeCommunityGoodwill(CHARACTER_COMMUNITY_INDEX from_community, ALife::_OBJECT_ID to_character,
	                             CHARACTER_GOODWILL delta_goodwill);

	void ClearRelations(ALife::_OBJECT_ID person_id);

	CHARACTER_GOODWILL GetCommunityRelation(CHARACTER_COMMUNITY_INDEX, CHARACTER_COMMUNITY_INDEX) const;
	void SetCommunityRelation(CHARACTER_COMMUNITY_INDEX index1, CHARACTER_COMMUNITY_INDEX index2,
	                          CHARACTER_GOODWILL goodwill);

private:
	CHARACTER_GOODWILL GetRankRelation(CHARACTER_RANK_VALUE, CHARACTER_RANK_VALUE) const;
	CHARACTER_GOODWILL GetReputationRelation(CHARACTER_REPUTATION_VALUE, CHARACTER_REPUTATION_VALUE) const;


	//реакцией на действия персонажей и соответствующее изменение отношения
public:

	//список действий актера, за которые начисляются
	//очки рейтинга, репутации или меняется отношения персонажа
	//к группировке
	enum ERelationAction
	{
		KILL = 0x00,
		//убийство персонажа
		ATTACK = 0x01,
		//атака персонажа
		FIGHT_HELP_HUMAN = 0x02,
		//помощь в драке персонажу с другим персонажем
		FIGHT_HELP_MONSTER = 0x04,
		//помощь в драке персонажу c монстром
		SOS_HELP = 0x08 //приход на помощь по сигналу SOS
	};

	void Action(CEntityAlive* from, CEntityAlive* to, ERelationAction action);

public:

	struct FIGHT_DATA
	{
		FIGHT_DATA();
		ALife::_OBJECT_ID attacker;
		ALife::_OBJECT_ID defender;
		float total_hit;
		u32 time;
		u32 time_old;

		u32 attack_time; //время фиксирования события "атака"
		ALife::ERelationType defender_to_attacker; //как относился атакованый к нападавшему во время начальной атаки
	};

	struct RELATION_MAP_SPOTS
	{
		RELATION_MAP_SPOTS();
		shared_str spot_names[ALife::eRelationTypeLast + 1];

		const shared_str& GetSpotName(ALife::ERelationType& type)
		{
			if (type < ALife::eRelationTypeLast)return spot_names[type];
			else return spot_names[ALife::eRelationTypeLast];
		};
	};

	//зарегистрировать драку (реакция на Hit в EntityAlive)
	void FightRegister(ALife::_OBJECT_ID attacker, ALife::_OBJECT_ID defender, ALife::ERelationType defender_to_attacker, float hit_amount);
	void UpdateFightRegister();

private:
	DEFINE_VECTOR(FIGHT_DATA, FIGHT_VECTOR, FIGHT_VECTOR_IT);
	static FIGHT_VECTOR* m_fight_registry;
	static FIGHT_VECTOR& fight_registry();

	FIGHT_DATA* FindFight(ALife::_OBJECT_ID object_id, bool by_attacker/* = true*/);
	static RELATION_MAP_SPOTS* m_spot_names;
public:
	const shared_str& GetSpotName(ALife::ERelationType& type);
	static CRelationRegistryWrapper& relation_registry();
	static void clear_relation_registry();
private:
	static CRelationRegistryWrapper* m_relation_registry;
};

#include "relation_registry_inline.h"
