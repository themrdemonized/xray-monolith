#include "pch_script.h"
#include "gameobject.h"
#include "script_game_object.h"
#include "../xrcdb/xr_collide_defs.h"
#include "material_manager.h"

struct script_rq_result
{
	CScriptGameObject* O;
	float range;
	int element;

	// Material of tri of ray query result
	str_c pMaterialName;
	u32 pMaterialFlags;

	// physics part
	float fPHFriction; // ?
	float fPHDamping; // ?
	float fPHSpring; // ?
	float fPHBounceStartVelocity; // ?
	float fPHBouncing; // ?
					   // shoot&bounce&visibility&flotation
	float fFlotationFactor; // 0.f - 1.f (1.f-полностью проходимый)
	float fShootFactor; // 0.f - 1.f (1.f-полностью простреливаемый)
	float fShootFactorMP; // 0.f - 1.f (1.f-полностью простреливаемый)
	float fBounceDamageFactor; // 0.f - 100.f
	float fInjuriousSpeed; // 0.f - ... (0.f-не отбирает здоровье (скорость уменьшения здоровья))
	float fVisTransparencyFactor; // 0.f - 1.f (1.f-полностью прозрачный)
	float fSndOcclusionFactor; // 0.f - 1.f (1.f-полностью слышен)
	float fDensityFactor;

	script_rq_result()
	{
		O = 0;
		range = 0;
		element = 0;
	};

	void set(collide::rq_result& R)
	{
		if (R.O)
		{
			CGameObject* go = smart_cast<CGameObject *>(R.O);
			if (go)
				O = go->lua_game_object();
		}
		range = R.range;
		element = R.element;

		// demonized: set material params of ray pick result
		//Msg("no object, check material");
		auto pTri = Level().ObjectSpace.GetStaticTris() + R.element;
		auto pMaterial = GMLib.GetMaterialByIdx(pTri->material);
		auto pMaterialFlagsRQ = pMaterial->Flags;
		//pTri = pTri;
		//pMaterial = pMaterial;
		pMaterialFlags = pMaterialFlagsRQ.flags;
		pMaterialName = pMaterial->m_Name.c_str();

		fPHFriction = pMaterial->fPHFriction;
		fPHDamping = pMaterial->fPHDamping;
		fPHSpring = pMaterial->fPHSpring;
		fPHBounceStartVelocity = pMaterial->fPHBounceStartVelocity;
		fPHBouncing = pMaterial->fPHBouncing;
		fFlotationFactor = pMaterial->fFlotationFactor;
		fShootFactor = pMaterial->fShootFactor;
		fShootFactorMP = pMaterial->fShootFactorMP;
		fBounceDamageFactor = pMaterial->fBounceDamageFactor;
		fInjuriousSpeed = pMaterial->fInjuriousSpeed;
		fVisTransparencyFactor = pMaterial->fVisTransparencyFactor;
		fSndOcclusionFactor = pMaterial->fSndOcclusionFactor;
		fDensityFactor = pMaterial->fDensityFactor;
	};
};

// class for performing ray pick
struct CRayPick
{
	Fvector start_position;
	Fvector direction;
	float range;
	collide::rq_target flags;
	script_rq_result result;
	CObject* ignore;

	CRayPick();
	CRayPick(const Fvector& P, const Fvector& D, float R, collide::rq_target F, CScriptGameObject* I);

	IC void set_position(Fvector& P) { start_position = P; };
	IC void set_direction(Fvector& D) { direction = D; };
	IC void set_range(float R) { range = R; };
	IC void set_flags(collide::rq_target F) { flags = F; };
	void set_ignore_object(CScriptGameObject* I) { if (I) ignore = smart_cast<CObject*>(&(I->object())); };

	bool query();

	IC script_rq_result get_result() { return result; };
	IC CScriptGameObject* get_object() { return result.O; };
	IC float get_distance() { return result.range; };
	IC int get_element() { return result.element; };
};
