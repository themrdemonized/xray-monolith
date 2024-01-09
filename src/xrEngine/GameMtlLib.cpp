//---------------------------------------------------------------------------
#include "stdafx.h"
#pragma hdrstop

#include "GameMtlLib.h"
//#include "../include/xrapi/xrapi.h"

#include "../xrCore/mezz_stringbuffer.h"

CGameMtlLibrary GMLib;
//CSound_manager_interface* Sound = NULL;
#ifdef _EDITOR
CGameMtlLibrary* PGMLib = NULL;
#endif
CGameMtlLibrary::CGameMtlLibrary()
{
	material_index = 0;
	material_pair_index = 0;
#ifndef _EDITOR
	material_count = 0;
#endif
	PGMLib = &GMLib;
}

void SGameMtl::Load(IReader& fs)
{
	R_ASSERT(fs.find_chunk(GAMEMTL_CHUNK_MAIN));
	ID = fs.r_u32();
	fs.r_stringZ(m_Name);

	if (fs.find_chunk(GAMEMTL_CHUNK_DESC))
	{
		fs.r_stringZ(m_Desc);
	}

	R_ASSERT(fs.find_chunk(GAMEMTL_CHUNK_FLAGS));
	Flags.assign(fs.r_u32());

	R_ASSERT(fs.find_chunk(GAMEMTL_CHUNK_PHYSICS));
	fPHFriction = fs.r_float();
	fPHDamping = fs.r_float();
	fPHSpring = fs.r_float();
	fPHBounceStartVelocity = fs.r_float();
	fPHBouncing = fs.r_float();

	R_ASSERT(fs.find_chunk(GAMEMTL_CHUNK_FACTORS));
	fShootFactor = fs.r_float();
	fBounceDamageFactor = fs.r_float();
	fVisTransparencyFactor = fs.r_float();
	fSndOcclusionFactor = fs.r_float();


	if (fs.find_chunk(GAMEMTL_CHUNK_FACTORS_MP))
		fShootFactorMP = fs.r_float();
	else
		fShootFactorMP = fShootFactor;

	if (fs.find_chunk(GAMEMTL_CHUNK_FLOTATION))
		fFlotationFactor = fs.r_float();

	if (fs.find_chunk(GAMEMTL_CHUNK_INJURIOUS))
		fInjuriousSpeed = fs.r_float();

	if (fs.find_chunk(GAMEMTL_CHUNK_DENSITY))
		fDensityFactor = fs.r_float();
}

//#define DEBUG_PRINT_MATERIAL

void CGameMtlLibrary::Load()
{
	string_path name;
	if (!FS.exist(name, _game_data_, GAMEMTL_FILENAME))
	{
		Log("! Can't find game material file: ", name);
		return;
	}

	R_ASSERT(material_pairs.empty());
	R_ASSERT(materials.empty());

	IReader* F = FS.r_open(name);
	IReader& fs = *F;

	R_ASSERT(fs.find_chunk(GAMEMTLS_CHUNK_VERSION));
	u16 version = fs.r_u16();
	if (GAMEMTL_CURRENT_VERSION != version)
	{
		Log("CGameMtlLibrary: invalid version. Library can't load.");
		FS.r_close(F);
		return;
	}

	R_ASSERT(fs.find_chunk(GAMEMTLS_CHUNK_AUTOINC));
	material_index = fs.r_u32();
	material_pair_index = fs.r_u32();

	materials.clear();
	material_pairs.clear();

	IReader* OBJ = fs.open_chunk(GAMEMTLS_CHUNK_MTLS);
	if (OBJ)
	{
		u32 count;
		for (IReader* O = OBJ->open_chunk_iterator(count); O; O = OBJ->open_chunk_iterator(count, O))
		{
			SGameMtl* M = xr_new<SGameMtl>();
			M->Load(*O);
			materials.push_back(M);
		}
		OBJ->close();
	}

	// demonized: loose gamemtl.xr loading
	string_path materialsLtxName;
	if (FS.exist(materialsLtxName, _game_data_, "materials\\materials", ".ltx"))
	{
#ifdef DEBUG_PRINT_MATERIAL
		Msg("found materials.ltx file %s", materialsLtxName);
#endif
		int biggestId = -1;
		auto materialsLtx = xr_new<CInifile>(materialsLtxName, TRUE);
		for (const auto& sec : materialsLtx->sections()) {
			SGameMtl* M;

			auto material = std::find_if(materials.begin(), materials.end(), [&sec](const SGameMtl* m) {
				return xr_strcmp(m->m_Name, sec->Name) == 0;
			});
			if (material == materials.end()) {
				M = xr_new<SGameMtl>();
				if (biggestId == 1) {
					for (const auto& m : materials) {
						if (m->ID > biggestId) {
							biggestId = m->ID;
						}
					}
				}
				M->ID = ++biggestId;
				M->m_Name = sec->Name;
				materials.push_back(M);

#ifdef DEBUG_PRINT_MATERIAL
				Msg("Adding new material %s, id %d", M->m_Name.c_str(), M->ID);
#endif
			} else {
				M = *material;

#ifdef DEBUG_PRINT_MATERIAL
				Msg("Changing existing material %s, id %d", M->m_Name.c_str(), M->ID);
#endif
			}

			if (materialsLtx->line_exist(M->m_Name, "desc"))					M->m_Desc = materialsLtx->r_string(M->m_Name, "desc");

			if (materialsLtx->line_exist(M->m_Name, "flag_breakable"))			M->Flags.set(SGameMtl::flBreakable, materialsLtx->r_bool(M->m_Name, "flag_breakable"));
			if (materialsLtx->line_exist(M->m_Name, "flag_bounceable"))			M->Flags.set(SGameMtl::flBounceable, materialsLtx->r_bool(M->m_Name, "flag_bounceable"));
			if (materialsLtx->line_exist(M->m_Name, "flag_skidmark"))			M->Flags.set(SGameMtl::flSkidmark, materialsLtx->r_bool(M->m_Name, "flag_skidmark"));
			if (materialsLtx->line_exist(M->m_Name, "flag_bloodmark"))			M->Flags.set(SGameMtl::flBloodmark, materialsLtx->r_bool(M->m_Name, "flag_bloodmark"));
			if (materialsLtx->line_exist(M->m_Name, "flag_climable"))			M->Flags.set(SGameMtl::flClimable, materialsLtx->r_bool(M->m_Name, "flag_climable"));
			if (materialsLtx->line_exist(M->m_Name, "flag_passable"))			M->Flags.set(SGameMtl::flPassable, materialsLtx->r_bool(M->m_Name, "flag_passable"));
			if (materialsLtx->line_exist(M->m_Name, "flag_dynamic"))			M->Flags.set(SGameMtl::flDynamic, materialsLtx->r_bool(M->m_Name, "flag_dynamic"));
			if (materialsLtx->line_exist(M->m_Name, "flag_liquid"))				M->Flags.set(SGameMtl::flLiquid, materialsLtx->r_bool(M->m_Name, "flag_liquid"));
			if (materialsLtx->line_exist(M->m_Name, "flag_suppress_shadows"))	M->Flags.set(SGameMtl::flSuppressShadows, materialsLtx->r_bool(M->m_Name, "flag_suppress_shadows"));
			if (materialsLtx->line_exist(M->m_Name, "flag_suppress_wallmarks"))	M->Flags.set(SGameMtl::flSuppressWallmarks, materialsLtx->r_bool(M->m_Name, "flag_suppress_wallmarks"));
			if (materialsLtx->line_exist(M->m_Name, "flag_actor_obstacle"))		M->Flags.set(SGameMtl::flActorObstacle, materialsLtx->r_bool(M->m_Name, "flag_actor_obstacle"));
			if (materialsLtx->line_exist(M->m_Name, "flag_bullet_no_ricochet"))	M->Flags.set(SGameMtl::flNoRicoshet, materialsLtx->r_bool(M->m_Name, "flag_bullet_no_ricochet"));
			if (materialsLtx->line_exist(M->m_Name, "flag_injurious"))			M->Flags.set(SGameMtl::flInjurious, materialsLtx->r_bool(M->m_Name, "flag_injurious"));
			if (materialsLtx->line_exist(M->m_Name, "flag_shootable"))			M->Flags.set(SGameMtl::flShootable, materialsLtx->r_bool(M->m_Name, "flag_shootable"));
			if (materialsLtx->line_exist(M->m_Name, "flag_transparent"))		M->Flags.set(SGameMtl::flTransparent, materialsLtx->r_bool(M->m_Name, "flag_transparent"));
			if (materialsLtx->line_exist(M->m_Name, "flag_slowdown"))			M->Flags.set(SGameMtl::flSlowDown, materialsLtx->r_bool(M->m_Name, "flag_slowdown"));

			if (materialsLtx->line_exist(M->m_Name, "friction"))				M->fPHFriction = materialsLtx->r_float(M->m_Name, "friction");
			if (materialsLtx->line_exist(M->m_Name, "damping"))					M->fPHDamping = materialsLtx->r_float(M->m_Name, "damping");
			if (materialsLtx->line_exist(M->m_Name, "spring"))					M->fPHSpring = materialsLtx->r_float(M->m_Name, "spring");
			if (materialsLtx->line_exist(M->m_Name, "bounce_start_velocity"))	M->fPHBounceStartVelocity = materialsLtx->r_float(M->m_Name, "bounce_start_velocity");
			if (materialsLtx->line_exist(M->m_Name, "bouncing"))				M->fPHBouncing = materialsLtx->r_float(M->m_Name, "bouncing");
			if (materialsLtx->line_exist(M->m_Name, "shoot_factor"))			M->fShootFactor = materialsLtx->r_float(M->m_Name, "shoot_factor");
			if (materialsLtx->line_exist(M->m_Name, "shoot_factor_mp"))			M->fShootFactorMP = materialsLtx->r_float(M->m_Name, "shoot_factor_mp");
			if (materialsLtx->line_exist(M->m_Name, "bounce_damage_factor"))	M->fBounceDamageFactor = materialsLtx->r_float(M->m_Name, "bounce_damage_factor");
			if (materialsLtx->line_exist(M->m_Name, "vis_transparency_factor"))	M->fVisTransparencyFactor = materialsLtx->r_float(M->m_Name, "vis_transparency_factor");
			if (materialsLtx->line_exist(M->m_Name, "sound_occlusion_factor"))	M->fSndOcclusionFactor = materialsLtx->r_float(M->m_Name, "sound_occlusion_factor");
			if (materialsLtx->line_exist(M->m_Name, "flotation_factor"))		M->fFlotationFactor = materialsLtx->r_float(M->m_Name, "flotation_factor");
			if (materialsLtx->line_exist(M->m_Name, "injurious_factor"))		M->fInjuriousSpeed = materialsLtx->r_float(M->m_Name, "injurious_factor");
			if (materialsLtx->line_exist(M->m_Name, "density_factor"))			M->fDensityFactor = materialsLtx->r_float(M->m_Name, "density_factor");
		}
		xr_delete(materialsLtx);
	}

#ifdef DEBUG_PRINT_MATERIAL
	for (const auto& mat : materials) {
		Msg("[%s]", mat->m_Name.c_str());
		Msg("id = %d", mat->ID);
		Msg("desc = %s", mat->m_Desc.c_str());
		Msg("flag_breakable = %s", mat->Flags.test(SGameMtl::flBreakable) ? "true" : "false");
		Msg("flag_bounceable = %s", mat->Flags.test(SGameMtl::flBounceable) ? "true" : "false");
		Msg("flag_skidmark = %s", mat->Flags.test(SGameMtl::flSkidmark) ? "true" : "false");
		Msg("flag_bloodmark = %s", mat->Flags.test(SGameMtl::flBloodmark) ? "true" : "false");
		Msg("flag_climable = %s", mat->Flags.test(SGameMtl::flClimable) ? "true" : "false");
		Msg("flag_passable = %s", mat->Flags.test(SGameMtl::flPassable) ? "true" : "false");
		Msg("flag_dynamic = %s", mat->Flags.test(SGameMtl::flDynamic) ? "true" : "false");
		Msg("flag_liquid = %s", mat->Flags.test(SGameMtl::flLiquid) ? "true" : "false");
		Msg("flag_suppress_shadows = %s", mat->Flags.test(SGameMtl::flSuppressShadows) ? "true" : "false");
		Msg("flag_suppress_wallmarks = %s", mat->Flags.test(SGameMtl::flSuppressWallmarks) ? "true" : "false");
		Msg("flag_actor_obstacle = %s", mat->Flags.test(SGameMtl::flActorObstacle) ? "true" : "false");
		Msg("flag_bullet_no_ricochet = %s", mat->Flags.test(SGameMtl::flNoRicoshet) ? "true" : "false");
		Msg("flag_injurious = %s", mat->Flags.test(SGameMtl::flInjurious) ? "true" : "false");
		Msg("flag_shootable = %s", mat->Flags.test(SGameMtl::flShootable) ? "true" : "false");
		Msg("flag_transparent = %s", mat->Flags.test(SGameMtl::flTransparent) ? "true" : "false");
		Msg("flag_slowdown = %s", mat->Flags.test(SGameMtl::flSlowDown) ? "true" : "false");
		Msg("friction = %.4f", mat->fPHFriction);
		Msg("damping = %.4f", mat->fPHDamping);
		Msg("spring = %.4f", mat->fPHSpring);
		Msg("bounce_start_velocity = %.4f", mat->fPHBounceStartVelocity);
		Msg("bouncing = %.4f", mat->fPHBouncing);
		Msg("shoot_factor = %.4f", mat->fShootFactor);
		Msg("shoot_factor_mp = %.4f", mat->fShootFactorMP);
		Msg("bounce_damage_factor = %.4f", mat->fBounceDamageFactor);
		Msg("vis_transparency_factor = %.4f", mat->fVisTransparencyFactor);
		Msg("sound_occlusion_factor = %.4f", mat->fSndOcclusionFactor);
		Msg("flotation_factor = %.4f", mat->fFlotationFactor);
		Msg("injurious_factor = %.4f", mat->fInjuriousSpeed);
		Msg("density_factor = %.4f", mat->fDensityFactor);
	}
#endif // DEBUG_PRINT_MATERIAL

	OBJ = fs.open_chunk(GAMEMTLS_CHUNK_MTLS_PAIR);
	if (OBJ)
	{
		u32 count;
		for (IReader* O = OBJ->open_chunk_iterator(count); O; O = OBJ->open_chunk_iterator(count, O))
		{
			SGameMtlPair* M = xr_new<SGameMtlPair>(this);
			M->Load(*O);
			material_pairs.push_back(M);
		}
		OBJ->close();
	}

	string_path materialPairsLtxName;
	if (FS.exist(materialPairsLtxName, _game_data_, "materials\\material_pairs", ".ltx"))
	{
#ifdef DEBUG_PRINT_MATERIAL
		Msg("found material_pairs.ltx file %s", materialPairsLtxName);
#endif
		int biggestId = -1;
		auto materialsLtx = xr_new<CInifile>(materialPairsLtxName, TRUE);
		for (const auto& sec : materialsLtx->sections()) {
			SGameMtlPair* M;
			
			std::string secStr = sec->Name.c_str();
			auto materials = splitStringMulti(secStr, "@", false, true);
			if (materials.size() < 2) {
				Msg("![material_pairs.ltx] encountered wrongly defined pair %s, two materials are required", secStr.c_str());
				continue;
			}

			int m1 = GetMaterialID(materials[0].c_str());
			int m2 = GetMaterialID(materials[1].c_str());

			if (m1 == GAMEMTL_NONE_ID) {
				Msg("![material_pairs.ltx] encountered unknown material %s in string %s, skip", materials[0].c_str(), secStr.c_str());
				continue;
			}
			if (m2 == GAMEMTL_NONE_ID) {
				Msg("![material_pairs.ltx] encountered unknown material %s in string %s, skip", materials[1].c_str(), secStr.c_str());
				continue;
			}

			auto material = std::find_if(material_pairs.begin(), material_pairs.end(), [&m1, &m2](SGameMtlPair* m) {
				return m1 == m->GetMtl0() && m2 == m->GetMtl1();
			});

			if (material == material_pairs.end()) {
				M = xr_new<SGameMtlPair>(this);
				if (biggestId == -1) {
					for (const auto& m : material_pairs) {
						if (m->ID > biggestId) {
							biggestId = m->ID;
						}
					}
				}
				M->ID = ++biggestId;
				M->ID_parent = -1;
				M->SetPair(m1, m2);
				material_pairs.push_back(M);

#ifdef DEBUG_PRINT_MATERIAL
				Msg("Adding new material pair %s | %s, id %d", GetMaterialByID(M->GetMtl0())->m_Name.c_str(), GetMaterialByID(M->GetMtl1())->m_Name.c_str(), M->ID);
#endif
			} else {
				M = *material;

#ifdef DEBUG_PRINT_MATERIAL
				Msg("Changing existing material pair %s | %s, id %d", GetMaterialByID(M->GetMtl0())->m_Name.c_str(), GetMaterialByID(M->GetMtl1())->m_Name.c_str(), M->ID);
#endif
			}

			if (materialsLtx->line_exist(sec->Name, "breaking_sounds")) {
				auto s = materialsLtx->r_string(sec->Name, "breaking_sounds");
				M->BreakingSoundsStr = s ? s : "";
				M->OwnProps.set(SGameMtlPair::flBreakingSounds, 1);
				M->CreateSoundsImpl(M->BreakingSounds, s);
			}
			if (materialsLtx->line_exist(sec->Name, "step_sounds")) {
				auto s = materialsLtx->r_string(sec->Name, "step_sounds");
				M->StepSoundsStr = s ? s : "";
				M->OwnProps.set(SGameMtlPair::flStepSounds, 1);
				M->CreateSoundsImpl(M->StepSounds, s);
			}
			if (materialsLtx->line_exist(sec->Name, "collide_sounds")) {
				auto s = materialsLtx->r_string(sec->Name, "collide_sounds");
				M->CollideSoundsStr = s ? s : "";
				M->OwnProps.set(SGameMtlPair::flCollideSounds, 1);
				M->CreateSoundsImpl(M->CollideSounds, s);
			}
			if (materialsLtx->line_exist(sec->Name, "collide_particles")) {
				auto s = materialsLtx->r_string(sec->Name, "collide_particles");
				M->CollideParticlesStr = s ? s : "";
				M->OwnProps.set(SGameMtlPair::flCollideParticles, 1);
				M->CreateParticlesImpl(M->CollideParticles, s);
			}
			if (materialsLtx->line_exist(sec->Name, "collide_marks")) {
				auto s = materialsLtx->r_string(sec->Name, "collide_marks");
				M->CollideMarksStr = s ? s : "";
				M->OwnProps.set(SGameMtlPair::flCollideMarks, 1);
				M->CreateMarksImpl(&*M->m_pCollideMarks, s);
			}
		}
		xr_delete(materialsLtx);
	}

#ifdef DEBUG_PRINT_MATERIAL
	for (const auto& mat : material_pairs) {
		Msg("[%s@%s]", GetMaterialByID(mat->mtl0)->m_Name.c_str(), GetMaterialByID(mat->mtl1)->m_Name.c_str());
		Msg("mtl0 = %d", mat->mtl0);
		Msg("mtl1 = %d", mat->mtl1);
		Msg("id = %d", mat->ID);
		Msg("id_parent = %d", mat->ID_parent);
		Msg("props = %d", mat->OwnProps.get());
		Msg("flag_breaking_sounds = %s", mat->OwnProps.test(SGameMtlPair::flBreakingSounds) ? "true" : "false");
		Msg("flag_step_sounds = %s", mat->OwnProps.test(SGameMtlPair::flStepSounds) ? "true" : "false");
		Msg("flag_collide_sounds = %s", mat->OwnProps.test(SGameMtlPair::flCollideSounds) ? "true" : "false");
		Msg("flag_collide_particles = %s", mat->OwnProps.test(SGameMtlPair::flCollideParticles) ? "true" : "false");
		Msg("flag_collide_marks = %s", mat->OwnProps.test(SGameMtlPair::flCollideMarks) ? "true" : "false");
		Msg("breaking_sounds = %s", mat->BreakingSoundsStr.c_str());
		Msg("step_sounds = %s", mat->StepSoundsStr.c_str());
		Msg("collide_sounds = %s", mat->CollideSoundsStr.c_str());
		Msg("collide_particles = %s", mat->CollideParticlesStr.c_str());
		Msg("collide_marks = %s", mat->CollideMarksStr.c_str());
	}
#endif // DEBUG_PRINT_MATERIAL

#ifndef _EDITOR
	material_count = (u32)materials.size();
	material_pairs_rt.resize(material_count * material_count, 0);
	for (GameMtlPairIt p_it = material_pairs.begin(); material_pairs.end() != p_it; ++p_it)
	{
		SGameMtlPair* S = *p_it;
		int idx0 = GetMaterialIdx(S->mtl0) * material_count + GetMaterialIdx(S->mtl1);
		int idx1 = GetMaterialIdx(S->mtl1) * material_count + GetMaterialIdx(S->mtl0);
		material_pairs_rt[idx0] = S;
		material_pairs_rt[idx1] = S;
	}
#endif

	/*
	 for (GameMtlPairIt p_it=material_pairs.begin(); material_pairs.end() != p_it; ++p_it){
	 SGameMtlPair* S = *p_it;
	 for (int k=0; k<S->StepSounds.size(); k++){
	 Msg("%40s - 0x%x", S->StepSounds[k].handle->file_name(), S->StepSounds[k].g_type);
	 }
	 }
	 */
	FS.r_close(F);
}

#ifdef GM_NON_GAME
SGameMtlPair::~SGameMtlPair ()
{
}
void SGameMtlPair::Load(IReader& fs)
{
    shared_str buf;

    R_ASSERT(fs.find_chunk(GAMEMTLPAIR_CHUNK_PAIR));
    mtl0 = fs.r_u32();
    mtl1 = fs.r_u32();
    ID = fs.r_u32();
    ID_parent = fs.r_u32();
    u32 own_mask = fs.r_u32();
    if (GAMEMTL_NONE_ID==ID_parent) OwnProps.one ();
    else OwnProps.assign (own_mask);

    R_ASSERT(fs.find_chunk(GAMEMTLPAIR_CHUNK_BREAKING));
    fs.r_stringZ (buf);
    BreakingSounds = buf.size()?*buf:"";

    R_ASSERT(fs.find_chunk(GAMEMTLPAIR_CHUNK_STEP));
    fs.r_stringZ(buf);
    StepSounds = buf.size() ? *buf : "";

    R_ASSERT(fs.find_chunk(GAMEMTLPAIR_CHUNK_COLLIDE));
    fs.r_stringZ(buf);
    CollideSounds = buf.size() ? *buf : "";
    fs.r_stringZ(buf);
    CollideParticles = buf.size() ? *buf : "";
    fs.r_stringZ(buf);
    CollideMarks = buf.size() ? *buf : "";
}
#endif

#ifdef DEBUG
LPCSTR SGameMtlPair::dbg_Name()
{
    static string256 nm;
    SGameMtl* M0 = GMLib.GetMaterialByID(GetMtl0());
    SGameMtl* M1 = GMLib.GetMaterialByID(GetMtl1());
    xr_sprintf(nm, sizeof(nm), "Pair: %s - %s", *M0->m_Name, *M1->m_Name);
    return nm;
}
#endif
