#include "stdafx.h"
#include "UIBinocularsVision.h"
#include "entity_alive.h"
#include "visual_memory_manager.h"
#include "actor.h"
#include "actor_memory.h"
#include "relation_registry.h"
#include "object_broker.h"

#include "game_base_space.h"
#include "Level.h"
#include "game_cl_base.h"
#include "AI/Monsters/BaseMonster/base_monster.h"
#include "../xrEngine/igame_persistent.h"

#define RECT_SIZE	11

extern u32 C_ON_ENEMY;
extern u32 C_ON_NEUTRAL;
extern u32 C_ON_FRIEND;

struct FindVisObjByObject
{
	u16 id;

	FindVisObjByObject(u16 _id): id(_id)
	{
	}

	bool operator ()(const CUIBinocularsVisionObject* vis)
	{
		return (id == vis->m_id);
	}
};

void CUIBinocularsVisionObject::create(const CUIBinocularsVision* vision)
{
	m_vision = vision;

	Frect r = {0, 0, vision->m_frame_size.x, vision->m_frame_size.y};
	m_lt.InitTexture(vision->m_frame_textures[0].c_str());
	m_lt.SetWndRect(r);
	m_lt.SetAlignment(waCenter);
	m_lt.SetStretchTexture(true);
	m_lb.InitTexture(vision->m_frame_textures[2].c_str());
	m_lb.SetWndRect(r);
	m_lb.SetAlignment(waCenter);
	m_lb.SetStretchTexture(true);
	m_rt.InitTexture(vision->m_frame_textures[1].c_str());
	m_rt.SetWndRect(r);
	m_rt.SetAlignment(waCenter);
	m_rt.SetStretchTexture(true);
	m_rb.InitTexture(vision->m_frame_textures[3].c_str());
	m_rb.SetWndRect(r);
	m_rb.SetAlignment(waCenter);
	m_rb.SetStretchTexture(true);

	AttachChild(&m_lt);
	AttachChild(&m_lb);
	AttachChild(&m_rt);
	AttachChild(&m_rb);

	u32 clr = subst_alpha(vision->m_frame_color, 128);
	m_lt.SetTextureColor(clr);
	m_lb.SetTextureColor(clr);
	m_rt.SetTextureColor(clr);
	m_rb.SetTextureColor(clr);

	cur_rect.set(0, 0, UI_BASE_WIDTH,UI_BASE_HEIGHT);

	m_flags.zero();
}

void CUIBinocularsVisionObject::Draw()
{
	if (m_flags.test(flVisObjNotValid)) return;

	inherited::Draw();
}

struct check_pred
{
	check_pred() { apos = Actor()->Position(); };
	Fvector apos;
	IC void operator()(CUIBinocularsVisionObject* _it) {
		if (!_it) return;

		auto object_ = Level().Objects.net_Find(_it->m_id);
		if (!object_)
		{
			_it->m_flags.set(flVisObjNotValid, TRUE);
			return;
		}

		CEntityAlive*	EA = smart_cast<CEntityAlive*>(object_);
		if (!EA) return;

		Fvector opos = Fvector(object_->Position());
		if (!EA->g_Alive() ||
			(0 > opos.sub(apos).dotproduct(Device.vCameraDirection)))
			_it->m_flags.set(flVisObjNotValid, TRUE);
	};
};

void CUIBinocularsVisionObject::Update()
{
	m_flags.set(flVisObjNotValid,TRUE);

	CObject* object = Level().Objects.net_Find(m_id);
	if (!object || !object->Visual()) return;

	Fbox b = object->Visual()->getVisData().box;

	Fmatrix xform;
	xform.mul(Device.mFullTransform, object->XFORM());
	Fvector2 mn = {flt_max,flt_max}, mx = {flt_min,flt_min};

	for (u32 k = 0; k < 8; ++k)
	{
		Fvector p;
		b.getpoint(k, p);
		xform.transform(p);
		mn.x = _min(mn.x, p.x);
		mn.y = _min(mn.y, p.y);
		mx.x = _max(mx.x, p.x);
		mx.y = _max(mx.y, p.y);
	}

	// Like fakescope.ps, magnify the circle, and only frame the targets whose center lands inside it
	if (m_vision->m_screen_zoom_radius > 0.f)
	{
		mn.mul(m_vision->m_screen_zoom);
		mx.mul(m_vision->m_screen_zoom);

		// Offset of the target's center from the middle of the screen, in screen heights like the radius
		// (projected coordinates span 2 per screen height)
		Fvector2 center;
		center.add(mn, mx).mul(0.5f);

		float screen_ratio = float(Device.dwWidth) / float(Device.dwHeight);
		Fvector2 offset;
		offset.set(center.x / 2.f * screen_ratio, center.y / 2.f);

		if (offset.magnitude() >= m_vision->m_screen_zoom_radius)
			return;
	}

	static Frect screen_rect = {-1.0f, -1.0f, 1.0f, 1.0f};

	Frect new_rect;
	new_rect.lt = mn;
	new_rect.rb = mx;

	if (FALSE == screen_rect.intersected(new_rect)) return;
	if (new_rect.in(screen_rect.lt) && new_rect.in(screen_rect.rb)) return;

	std::swap(mn.y, mx.y);
	mn.x = (1.f + mn.x) / 2.f * UI_BASE_WIDTH;
	mx.x = (1.f + mx.x) / 2.f * UI_BASE_WIDTH;
	mn.y = (1.f - mn.y) / 2.f * UI_BASE_HEIGHT;
	mx.y = (1.f - mx.y) / 2.f * UI_BASE_HEIGHT;

	if (mx.x - mn.x < m_vision->m_frame_size.x)
		mx.x = mn.x + m_vision->m_frame_size.x;

	if (mx.y - mn.y < m_vision->m_frame_size.y)
		mx.y = mn.y + m_vision->m_frame_size.y;

	if (m_flags.is(flTargetLocked))
	{
		cur_rect.lt.set(mn);
		cur_rect.rb.set(mx);
	}
	else
	{
		// Past 1 the frame would overshoot and diverge, so a high speed locks at once
		float progress = _min(m_vision->m_rotating_speed * Device.fTimeDelta, 1.f);
		cur_rect.lt.x += (mn.x - cur_rect.lt.x) * progress;
		cur_rect.lt.y += (mn.y - cur_rect.lt.y) * progress;
		cur_rect.rb.x += (mx.x - cur_rect.rb.x) * progress;
		cur_rect.rb.y += (mx.y - cur_rect.rb.y) * progress;
		if (mn.similar(cur_rect.lt, 2.f) && mx.similar(cur_rect.rb, 2.f))
		{
			// target locked
			m_flags.set(flTargetLocked,TRUE);
			u32 clr = subst_alpha(m_lt.GetTextureColor(), 255);

			//-----------------------------------------------------
			CActor* pActor = NULL;
			if (IsGameTypeSingle()) pActor = Actor();
			else
			{
				if (Level().CurrentViewEntity())
				{
					pActor = smart_cast<CActor*>(Level().CurrentViewEntity());
				}
			}
			if (pActor)
			{
				//-----------------------------------------------------

				CInventoryOwner* our_inv_owner = smart_cast<CInventoryOwner*>(pActor);
				CInventoryOwner* others_inv_owner = smart_cast<CInventoryOwner*>(object);
				CBaseMonster* monster = smart_cast<CBaseMonster*>(object);

				if (our_inv_owner && others_inv_owner && !monster)
				{
					if (IsGameTypeSingle())
					{
						switch (RELATION_REGISTRY().GetRelationType(others_inv_owner, our_inv_owner))
						{
						case ALife::eRelationTypeEnemy:
							clr = m_vision->m_enemy_color;
							break;
						case ALife::eRelationTypeNeutral:
							clr = m_vision->m_neutral_color;
							break;
						case ALife::eRelationTypeFriend:
							clr = m_vision->m_friend_color;
							break;
						}
					}
					else
					{
						CEntityAlive* our_ealive = smart_cast<CEntityAlive*>(pActor);
						CEntityAlive* others_ealive = smart_cast<CEntityAlive*>(object);
						if (our_ealive && others_ealive)
						{
							if (Game().IsEnemy(our_ealive, others_ealive))
								clr = m_vision->m_enemy_color;
							else
								clr = m_vision->m_friend_color;
						}
					}
				}
			}

			m_lt.SetTextureColor(clr);
			m_lb.SetTextureColor(clr);
			m_rt.SetTextureColor(clr);
			m_rb.SetTextureColor(clr);
		}
	}

	m_lt.SetWndPos(Fvector2().set((cur_rect.lt.x), (cur_rect.lt.y)));
	m_lb.SetWndPos(Fvector2().set((cur_rect.lt.x), (cur_rect.rb.y)));
	m_rt.SetWndPos(Fvector2().set((cur_rect.rb.x), (cur_rect.lt.y)));
	m_rb.SetWndPos(Fvector2().set((cur_rect.rb.x), (cur_rect.rb.y)));

	m_flags.set(flVisObjNotValid, FALSE);
}


CUIBinocularsVision::CUIBinocularsVision(LPCSTR lt, LPCSTR rt, LPCSTR lb, LPCSTR rb, const Fvector2& frame_size)
{
	m_frame_textures[0] = lt;
	m_frame_textures[1] = rt;
	m_frame_textures[2] = lb;
	m_frame_textures[3] = rb;
	m_frame_size = frame_size;
	m_frame_color = color_rgba(178, 178, 178, 255);
	m_rotating_speed = 8.f;
	m_screen_zoom = 1.f;
	m_screen_zoom_radius = 0.f;
	SetRelationColors(C_ON_FRIEND, C_ON_NEUTRAL, C_ON_ENEMY);
}

CUIBinocularsVision::CUIBinocularsVision(const shared_str& sect) : CUIBinocularsVision(
	"ui_binoculars_vision_lt",
	"ui_binoculars_vision_rt",
	"ui_binoculars_vision_lb",
	"ui_binoculars_vision_rb",
	Fvector2().set(RECT_SIZE, RECT_SIZE))
{
	Load(sect);
}

void CUIBinocularsVision::Update()
{
	if (g_dedicated_server)
		return;

	//-----------------------------------------------------
	const CActor* pActor = NULL;
	if (IsGameTypeSingle()) pActor = Actor();
	else
	{
		if (Level().CurrentViewEntity())
		{
			pActor = smart_cast<const CActor*>(Level().CurrentViewEntity());
		}
	}
	if (!pActor) return;
	//-----------------------------------------------------
	const CVisualMemoryManager::VISIBLES& vVisibles = pActor->memory().visual().objects();

	VIS_OBJECTS_IT it = m_active_objects.begin();
	for (; it != m_active_objects.end(); ++it)
		(*it)->m_flags.set(flVisObjNotValid, TRUE);


	CVisualMemoryManager::VISIBLES::const_iterator v_it = vVisibles.begin();
	for (; v_it != vVisibles.end(); ++v_it)
	{
		const CObject* _object_ = (*v_it).m_object;
		if (!pActor->memory().visual().visible_right_now(smart_cast<const CGameObject*>(_object_)))
			continue;

		CObject* object_ = const_cast<CObject*>(_object_);


		CEntityAlive* EA = smart_cast<CEntityAlive*>(object_);
		if (!EA || !EA->g_Alive()) continue;

		Fvector opos = Fvector(object_->Position());
		if (0 > opos.sub(Actor()->Position()).dotproduct(Device.vCameraDirection)) continue;

		FindVisObjByObject f(object_->ID());
		VIS_OBJECTS_IT found;
		found = std::find_if(m_active_objects.begin(), m_active_objects.end(), f);

		if (found != m_active_objects.end())
		{
			(*found)->m_flags.set(flVisObjNotValid,FALSE);
		}
		else
		{
			m_active_objects.push_back(xr_new<CUIBinocularsVisionObject>());
			CUIBinocularsVisionObject* new_vis_obj = m_active_objects.back();
			new_vis_obj->m_flags.set(flVisObjNotValid,FALSE);
			new_vis_obj->m_id = object_->ID();
			new_vis_obj->create(this);
			new_vis_obj->SetAutoDelete(true);
			AttachChild(new_vis_obj);

			m_found_snd.play(NULL, sm_2D);
		}
	}

	/*
	std::sort(m_active_objects.begin(), m_active_objects.end());

	while (m_active_objects.size() && m_active_objects.back()->m_flags.test(flVisObjNotValid))
	{
		xr_delete(m_active_objects.back());
		m_active_objects.pop_back();
	}
	*/

	// death or invis
	for_each(m_active_objects.begin(), m_active_objects.end(), check_pred());

	m_active_objects.erase(
		remove_if(
			m_active_objects.begin(),
			m_active_objects.end(),
			[this](CUIBinocularsVisionObject* _it) {
			if (!_it) return true;
			bool res = _it->m_flags.test(flVisObjNotValid);
			if (res) DetachChild(_it);
			return res;
		}),
		m_active_objects.end()
		);

	it = m_active_objects.begin();
	for (; it != m_active_objects.end(); ++it)
	{
		CUIBinocularsVisionObject* visObj = (*it);

		BOOL bLocked = visObj->m_flags.test(flTargetLocked);

		(*it)->Update();

		if (bLocked != visObj->m_flags.test(flTargetLocked))
			m_catch_snd.play(NULL, sm_2D);
	}
}

void CUIBinocularsVision::Show(bool status)
{
	inherited::Show(status);

	if (!status)
	{
		for (CUIBinocularsVisionObject* object : m_active_objects)
			DetachChild(object);
		m_active_objects.clear();
	}
}

void CUIBinocularsVision::Load(const shared_str& section)
{
	SetSpeed(pSettings->r_float(section, "vis_frame_speed"));
	SetFrameColor(pSettings->r_fcolor(section, "vis_frame_color").get());
	SetFoundSound(READ_IF_EXISTS(pSettings, r_string, section, "found_snd", ""));
	SetCatchSound(READ_IF_EXISTS(pSettings, r_string, section, "catch_snd", ""));
}

void CUIBinocularsVision::SetRelationColors(u32 friend_color, u32 neutral_color, u32 enemy_color)
{
	m_friend_color = friend_color;
	m_neutral_color = neutral_color;
	m_enemy_color = enemy_color;
}

static void load_vision_sound(ref_sound& snd, LPCSTR path)
{
	snd.destroy();
	if (xr_strlen(path) && xr_strcmp(path, "none"))
		snd.create(path, st_Effect, SOUND_TYPE_NO_SOUND);
}

void CUIBinocularsVision::SetFoundSound(LPCSTR path)
{
	load_vision_sound(m_found_snd, path);
}

void CUIBinocularsVision::SetCatchSound(LPCSTR path)
{
	load_vision_sound(m_catch_snd, path);
}
