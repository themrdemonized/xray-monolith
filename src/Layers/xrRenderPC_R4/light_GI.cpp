#include "StdAfx.h"
#include "../xrRender/light.h"

/*
	LV:
	
	It's not clear to me how this GI works. 
	Looks to be sort of "photon mapping" - yet, I don't understand why GSC
	reflects anything. 
	
	I would just try to implement diffuse GI using GSC functions, with more 
	agressive culling - Remember, we are raytracing lights, not emmisive surfaces
	like in 90% of actual PT/SSPT implementations. 
	Keeping this comment here for future updates. 
*/

IC bool pred_LI(const light_indirect& A, const light_indirect& B)
{
	return A.E > B.E;
}

void light::gi_generate()
{
	indirect.clear();
	indirect_photons = ps_r2_ls_flags.test(R2FLAG_GI) ? ps_r2_GI_photons : 0;

	CRandom random;
	random.seed(0x12071980); //Get random seed 

	xrXRC& xrc = RImplementation.Sectors_xrc;
	CDB::MODEL* model = g_pGameLevel->ObjectSpace.GetStaticModel();
	CDB::TRI* tris = g_pGameLevel->ObjectSpace.GetStaticTris();
	Fvector* verts = g_pGameLevel->ObjectSpace.GetStaticVerts();
	xrc.ray_options(CDB::OPT_CULL | CDB::OPT_ONLYNEAREST);

	for (int it = 0; it < int(indirect_photons * 8); it++)
	{
		//Get light direction
		Fvector dir, idir;
		switch (flags.type)
		{
		case IRender_Light::POINT: dir.random_dir(random);
			break;
		case IRender_Light::SPOT: dir.random_dir(direction, cone, random);
			break;
		case IRender_Light::OMNIPART: dir.random_dir(direction, cone, random);
			break;
		}
		dir.normalize(); //Normalize it
		
		//Get ray data (?)
		xrc.ray_query(model, position, dir, range);
		
		//Not too much rays? Nice, let's continue
		if (!xrc.r_count()) 
			continue;
		
		//Start actual raytracing
		CDB::RESULT* R = RImplementation.Sectors_xrc.r_begin();
		CDB::TRI& T = tris[R->id];
		Fvector Tv[3] = {verts[T.verts[0]], verts[T.verts[1]], verts[T.verts[2]]};
		Fvector TN;
		TN.mknormal(Tv[0], Tv[1], Tv[2]);
		float dot = TN.dotproduct(idir.invert(dir));

		light_indirect LI;
		LI.P.mad(position, dir, R->range); //Position of the light
		LI.D.reflect(dir, TN); //Cosine (?)
		LI.E = dot * (1 - R->range / range); //Looks to be light attenuation or something
		
		//"Limiter"?
		if (LI.E < ps_r2_GI_clip) 
			continue;
		
		LI.S = spatial.sector; //. BUG

		indirect.push_back(LI);
	}

	// sort & clip
	std::sort(indirect.begin(), indirect.end(), pred_LI);
	if (indirect.size() > indirect_photons)
		indirect.erase(indirect.begin() + indirect_photons, indirect.end());

	// normalize
	if (indirect.size())
	{
		float target_E = ps_r2_GI_refl;
		float total_E = 0;
		for (u32 it = 0; it < indirect.size(); it++)
			total_E += indirect[it].E;
		float scale_E = target_E / total_E;
		for (u32 it = 0; it < indirect.size(); it++)
			indirect[it].E *= scale_E;
	}
}
