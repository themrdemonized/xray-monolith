#include "stdafx.h"
#include "hudtarget.h"

#include "../xrEngine/Environment.h"
#include "../xrEngine/CustomHUD.h"
#include "Entity.h"
#include "Actor.h"
#include "Weapon.h"
#include "WeaponKnife.h"
#include "player_hud.h"
#include "Missile.h"
#include "level.h"
#include "game_cl_base.h"
#include "../xrEngine/igame_persistent.h"
#include "script_render_device.h"
#include "HUDManager.h"

#include "ui_base.h"
#include "InventoryOwner.h"
#include "relation_registry.h"
#include "character_info.h"

#include "string_table.h"
#include "entity_alive.h"

#include "inventory_item.h"
#include "inventory.h"

#include <ai/monsters/poltergeist/poltergeist.h>


u32 C_ON_ENEMY D3DCOLOR_RGBA(0xff, 0, 0, 0x80);
u32 C_ON_NEUTRAL D3DCOLOR_RGBA(0xff, 0xff, 0x80, 0x80);
u32 C_ON_FRIEND D3DCOLOR_RGBA(0, 0xff, 0, 0x80);

#define C_DEFAULT	D3DCOLOR_RGBA(0xff,0xff,0xff,0x80)

#define SHOW_INFO_SPEED		0.5f
#define HIDE_INFO_SPEED		10.f

static float recon_mindist()
{
	return 2.f;
}

static float recon_maxdist()
{
	return 50.f;
}

static float recon_minspeed()
{
	return 0.5f;
}

static float recon_maxspeed()
{
	return 10.f;
}

static float lerp(float a, float b, float t)
{
	clamp(t, 0.f, 1.f);
	return a * (1 - t) + b * t;
}

CHUDTarget::CHUDTarget()
{
	fuzzyShowInfo = 0.f;

	crosshairPos = Fvector();
	crosshairOpacity = 1.f;

	occludedOpacity = .6f;

	occlusionFadeRate = 20.f;
	distanceLerpRate = 35.f;

	Load();
	m_bShowCrosshair = false;
}

CHUDTarget::~CHUDTarget()
{
}

void CHUDTarget::Load()
{
	occludedOpacity = pSettings->r_float(HUD_CURSOR_SECTION, "occluded_opacity");
	occlusionFadeRate = pSettings->r_float(HUD_CURSOR_SECTION, "occlusion_fade_rate");
	distanceLerpRate = pSettings->r_float(HUD_CURSOR_SECTION, "distance_lerp_rate");
	HUDCrosshair.Load();
}

void CHUDTarget::ShowCrosshair(bool b)
{
	m_bShowCrosshair = b;
}

extern ENGINE_API BOOL g_bRendering;
u32 g_crosshair_color = C_DEFAULT;

float CHUDTarget::GetUIDist() const
{
	const SPickParam& pp = Actor()->GetPick();
	float dist = pp.result.range;
	if (!pp.barrel_blocked)
	{
		dist -= pp.barrel_dist;
	}
	return dist;
}

float CHUDTarget::GetTargetOpacity() const
{
	const SPickParam& pp = Actor()->GetPick();
	if (pp.barrel_blocked)
	{
		return 1.f;
	}

	// If the barrel is not occluded...
	// Test whether the aim point is occluded
	Fvector dir = Fvector().sub(
		Fvector().add(pp.defs.start, Fvector().mul(pp.defs.dir, GetUIDist())),
		Device.vCameraPosition
	);

	float dist = dir.magnitude();
	dir.normalize();
	SPickParam op = SPickParam();
	op.defs.start = Device.vCameraPosition;
	op.defs.dir = dir;
	op.defs.range = dist * 0.99f;
	if (!HUD().DoPick(op))
	{
		return 1.f;
	}

	// If it is, apply fade
	return occludedOpacity;
}

void CHUDTarget::IntegratePosition()
{
	const SPickParam& pp = Actor()->GetPick();

	// Transform ray start and direction into camera space
	Fvector pos = pp.defs.start;
	Fvector dir = pp.defs.dir;
	Device.mView.transform_tiny(pos);
	Device.mView.transform_dir(dir);

	float dist = GetUIDist();

	// Interpolate crosshair position toward target
	crosshairPos.lerp(
		crosshairPos,
		Fvector().add(pos, Fvector().mul(dir, dist)),
		Device.fTimeDelta * distanceLerpRate
	);
}

void CHUDTarget::IntegrateOpacity()
{
	float opacity_target = GetTargetOpacity();

	// Interpolate opacity offset toward target
	crosshairOpacity = lerp(crosshairOpacity, opacity_target, Device.fTimeDelta * occlusionFadeRate);
}

void CHUDTarget::Render()
{
	BOOL b_do_rendering = (psHUD_Flags.is(HUD_CROSSHAIR | HUD_CROSSHAIR_RT | HUD_CROSSHAIR_RT2));

	if (!b_do_rendering)
		return;

	VERIFY(g_bRendering);

	CObject* O = Level().CurrentEntity();
	if (0 == O) return;
	CEntity* E = smart_cast<CEntity*>(O);
	if (0 == E) return;

	IntegratePosition();
	IntegrateOpacity();

	const SPickParam& pp = Actor()->GetPick();

	// Construct animated aim point matrix from interpolated position and barrel roll
	Fvector hpb_barrel;
	pp.barrel_matrix.getHPB(hpb_barrel);

	Fvector hpb_cam;
	Device.mInvView.getHPB(hpb_cam);

	Fmatrix mat_aim = Fmatrix().identity();
	mat_aim.mulB_43(Device.mInvView);
	mat_aim.mulB_43(Fmatrix().translate(crosshairPos));
	mat_aim.mulB_43(Fmatrix().setHPB(0, 0, hpb_barrel.z - hpb_cam.z));

	float result_dist = pp.result.range - pp.barrel_dist;
	clamp(result_dist, 0.f, result_dist);

	Fvector4 pt;
	Device.mFullTransform.transform(pt, mat_aim.c);
	pt.y = -pt.y;

	// Crosshair color
	u32 color_readout = C_DEFAULT;

	// Readout font
	CGameFont* F = UI().Font().pFontGraffiti19Russian;
	F->SetAligment(CGameFont::alCenter);
	F->OutSetI(pt.x, pt.y + 0.05f);

	if (psHUD_Flags.test(HUD_CROSSHAIR_DIST))
		F->OutSkip();

	if (psHUD_Flags.test(HUD_INFO))
	{
		bool const is_poltergeist = pp.result.O && !!smart_cast<CPoltergeist*>(pp.result.O);

		if ((pp.result.O && pp.result.O->getVisible()) || is_poltergeist)
		{
			CEntityAlive* EA = smart_cast<CEntityAlive*>(pp.result.O);
			CEntityAlive* pCurEnt = smart_cast<CEntityAlive*>(Level().CurrentEntity());
			PIItem l_pI = smart_cast<PIItem>(pp.result.O);

			if (IsGameTypeSingle())
			{
				CInventoryOwner* our_inv_owner = smart_cast<CInventoryOwner*>(pCurEnt);

				if (EA && EA->g_Alive() && EA->cast_base_monster())
				{
					color_readout = C_ON_ENEMY;
				}
				else if (EA && EA->g_Alive() && !EA->cast_base_monster())
				{
					CInventoryOwner* others_inv_owner = smart_cast<CInventoryOwner*>(EA);

					if (our_inv_owner && others_inv_owner)
					{
						switch (RELATION_REGISTRY().GetRelationType(others_inv_owner, our_inv_owner))
						{
						case ALife::eRelationTypeEnemy:
							color_readout = C_ON_ENEMY;
							break;
						case ALife::eRelationTypeNeutral:
							color_readout = C_ON_NEUTRAL;
							break;
						case ALife::eRelationTypeFriend:
							color_readout = C_ON_FRIEND;
							break;
						}

						if (fuzzyShowInfo > 0.5f)
						{
							CStringTable strtbl;
							F->SetColor(subst_alpha(color_readout, u8(iFloor(255.f * (fuzzyShowInfo - 0.5f) * 2.f))));
							F->OutNext("%s", *strtbl.translate(others_inv_owner->Name()));
							F->OutNext("%s", *strtbl.translate(others_inv_owner->CharacterInfo().Community().id()));
						}
					}

					fuzzyShowInfo += SHOW_INFO_SPEED * Device.fTimeDelta;
				}
				else if (l_pI && our_inv_owner && result_dist < 2.0f * 2.0f)
				{
					if (fuzzyShowInfo > 0.5f && l_pI->NameItem())
					{
						F->SetColor(subst_alpha(color_readout, u8(iFloor(255.f * (fuzzyShowInfo - 0.5f) * 2.f))));
						F->OutNext("%s", l_pI->NameItem());
					}
					fuzzyShowInfo += SHOW_INFO_SPEED * Device.fTimeDelta;
				}
			}
			else
			{
				if (EA && (EA->GetfHealth() > 0))
				{
					if (pCurEnt && GameID() & eGameIDSingle)
					{
						if (GameID() & eGameIDDeathmatch) color_readout = C_ON_ENEMY;
						else
						{
							if (EA->g_Team() != pCurEnt->g_Team()) color_readout = C_ON_ENEMY;
							else color_readout = C_ON_FRIEND;
						};
						if (result_dist >= recon_mindist() && result_dist <= recon_maxdist())
						{
							float ddist = (result_dist - recon_mindist()) / (recon_maxdist() - recon_mindist());
							float dspeed = recon_minspeed() + (recon_maxspeed() - recon_minspeed()) * ddist;
							fuzzyShowInfo += Device.fTimeDelta / dspeed;
						}
						else
						{
							if (result_dist < recon_mindist())
								fuzzyShowInfo += recon_minspeed() * Device.fTimeDelta;
							else
								fuzzyShowInfo = 0;
						};

						if (fuzzyShowInfo > 0.5f)
						{
							clamp(fuzzyShowInfo, 0.f, 1.f);
							int alpha_C = iFloor(255.f * (fuzzyShowInfo - 0.5f) * 2.f);
							u8 alpha_b = u8(alpha_C & 0x00ff);
							F->SetColor(subst_alpha(color_readout, alpha_b));
							F->OutNext("%s", *pp.result.O->cName());
						}
					}
				};
			};
		}
		else
		{
			fuzzyShowInfo -= HIDE_INFO_SPEED * Device.fTimeDelta;
		}
		clamp(fuzzyShowInfo, 0.f, 1.f);
	}

	if (psHUD_Flags.test(HUD_CROSSHAIR_DIST))
	{
		F->OutSetI(pt.x, pt.y + 0.05f);
		F->SetColor(color_readout);
#ifdef DEBUG
		F->OutNext("%4.1f - %4.2f - %d", result_dist, PP.power, PP.pass);
#else
		F->OutNext("%4.1f", result_dist);
#endif
	}

	// Use the crosshair color unless the readout color is non-default
	u32 color_crosshair = color_readout == C_DEFAULT ? g_crosshair_color : color_readout;

	// Modulate color alpha
	DWORD alpha_mask = 0xff000000;
	color_crosshair = (color_crosshair | alpha_mask)
		& ((DWORD)(alpha_mask * crosshairOpacity) | (~alpha_mask));

	if (m_bShowCrosshair)
	{
		// Update the crosshair's transform and color, and draw it
		HUDCrosshair.SetTransform(mat_aim);
		HUDCrosshair.SetColor(color_crosshair);
		HUDCrosshair.OnRender();
	}
}
