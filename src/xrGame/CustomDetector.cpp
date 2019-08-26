#include "stdafx.h"
#include "customdetector.h"
#include "ui/ArtefactDetectorUI.h"
#include "hudmanager.h"
#include "inventory.h"
#include "level.h"
#include "map_manager.h"
#include "ActorEffector.h"
#include "actor.h"
#include "ui/UIWindow.h"
#include "player_hud.h"
#include "weapon.h"

ITEM_INFO::ITEM_INFO()
{
	snd_time = 0.0f;
	cur_period = 0.0f;
	pParticle = NULL;
	curr_ref = NULL;
}

ITEM_INFO::~ITEM_INFO()
{
	if (pParticle)
		CParticlesObject::Destroy(pParticle);
}

CCustomDetector::CCustomDetector(){}

CCustomDetector::~CCustomDetector()
{
	m_artefacts.destroy();
}

void CCustomDetector::Load(LPCSTR section)
{
	inherited::Load(section);

	m_fAfDetectRadius = pSettings->r_float(section, "af_radius");
	m_fAfVisRadius = pSettings->r_float(section, "af_vis_radius");
	m_artefacts.load(section, "af");
}

void CCustomDetector::shedule_Update(u32 dt)
{
	inherited::shedule_Update(dt);

	if (!IsWorking())
		return;

	Fvector P;
	P.set(H_Parent()->Position());

	m_artefacts.feel_touch_update(P, m_fAfDetectRadius);
}

void CCustomDetector::UpdateWork()
{
	UpdateAf();

	inherited::UpdateWork();
}

void CCustomDetector::OnH_B_Independent(bool just_before_destroy)
{
	inherited::OnH_B_Independent(just_before_destroy);

	m_artefacts.clear();
}

#include "game_base_space.h"
bool CAfList::feel_touch_contact(CObject* O)
{
	TypesMapIt it = m_TypesMap.find(O->cNameSect());

	bool res = (it != m_TypesMap.end());
	if (res)
	{
		CArtefact* pAf = smart_cast<CArtefact*>(O);

		if (pAf->GetAfRank() > m_af_rank)
			res = false;
	}
	return res;
}
