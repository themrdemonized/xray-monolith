#include "stdafx.h"
#include "raypick.h"
#include "level.h"
#include "material_manager.h"

CRayPick::CRayPick()
{
	start_position.set(0, 0, 0);
	direction.set(0, 0, 0);
	range = 0;
	flags = collide::rq_target::rqtNone;
	ignore = NULL;
};

CRayPick::CRayPick(const Fvector& P, const Fvector& D, float R, collide::rq_target F, CScriptGameObject* I)
{
	start_position.set(P);
	direction.set(D);
	range = R;
	flags = F;
	ignore = NULL;
	if (I)
		ignore = smart_cast<CObject*>(&(I->object()));
};

bool CRayPick::query()
{
	collide::rq_result R;
	if (Level().ObjectSpace.RayPick(start_position, direction, range, flags, R, ignore))
	{
		result.set(R);
		if (!R.O) {
			//Msg("no object, check material");
			auto pTri = Level().ObjectSpace.GetStaticTris() + R.element;
			auto pMaterial = GMLib.GetMaterialByIdx(pTri->material);
			auto pMaterialFlags = pMaterial->Flags;
			//result.pTri = pTri;
			//result.pMaterial = pMaterial;
			result.pMaterialFlags = pMaterialFlags.flags;
			result.pMaterialName = pMaterial->m_Name.c_str();

			result.fPHFriction = pMaterial->fPHFriction;
			result.fPHDamping = pMaterial->fPHDamping;
			result.fPHSpring = pMaterial->fPHSpring;
			result.fPHBounceStartVelocity = pMaterial->fPHBounceStartVelocity;
			result.fPHBouncing = pMaterial->fPHBouncing;
			result.fFlotationFactor = pMaterial->fFlotationFactor;
			result.fShootFactor = pMaterial->fShootFactor;
			result.fShootFactorMP = pMaterial->fShootFactorMP;
			result.fBounceDamageFactor = pMaterial->fBounceDamageFactor;
			result.fInjuriousSpeed = pMaterial->fInjuriousSpeed;
			result.fVisTransparencyFactor = pMaterial->fVisTransparencyFactor;
			result.fSndOcclusionFactor = pMaterial->fSndOcclusionFactor;
			result.fDensityFactor = pMaterial->fDensityFactor;
		}
		return true;
	}
	else
		return false;
}
