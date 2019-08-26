//---------------------------------------------------------------------------
#include "stdafx.h"
#pragma hdrstop

#include "../xrEngine/GameMtlLib.h"

void DestroySounds(SoundVec& lst)
{
	for (SoundIt it = lst.begin(); lst.end() != it; ++it)
		it->destroy();
}

/*
void DestroyMarks(ShaderVec& lst)
{
for (ShaderIt it=lst.begin(); lst.end() != it; ++it)
it->destroy();
}
*/

void DestroyPSs(PSVec& lst)
{
	// for (PSIt it=lst.begin(); lst.end() != it; ++it)
	// Device.Resources->Delete(*it);
}

void CreateSounds(SoundVec& lst, LPCSTR buf)
{
	string128 tmp;
	int cnt = _GetItemCount(buf);
	R_ASSERT(cnt <= GAMEMTL_SUBITEM_COUNT + 2);

	for (int k = 0; k < cnt; ++k)
	{
		_GetItem(buf, k, tmp);

		if (strstr(tmp, "*"))
		{
			xr_strcat(tmp, ".ogg");
			FS_FileSet fset;
			FS.file_list(fset, "$game_sounds$", FS_ListFiles, tmp);

			for (FS_FileSet::iterator it = fset.begin(); it != fset.end(); it++)
			{
				string128 name;
				xr_strcpy(name, sizeof(name), (*it).name.c_str());
				*strext(name) = 0;

				ref_sound snd;
				snd.create(name, st_Effect, sg_SourceType);
				lst.push_back(snd);
			}
		}
		else
		{
			ref_sound snd;
			snd.create(tmp, st_Effect, sg_SourceType);
			lst.push_back(snd);
		}
	}
}

/*
void CreateMarks(ShaderVec& lst, LPCSTR buf)
{
string256 tmp;
int cnt =_GetItemCount(buf); R_ASSERT(cnt<=GAMEMTL_SUBITEM_COUNT);
ref_shader s;
for (int k=0; k<cnt; ++k)
{
s.create ("effects\\wallmark",_GetItem(buf,k,tmp));
lst.push_back (s);
}
}
*/
void CreateMarks(IWallMarkArray* pMarks, LPCSTR buf)
{
	string256 tmp;
	int cnt = _GetItemCount(buf);
	R_ASSERT(cnt <= GAMEMTL_SUBITEM_COUNT);
	for (int k = 0; k < cnt; ++k)
		pMarks->AppendMark(_GetItem(buf, k, tmp));
}


void CreatePSs(PSVec& lst, LPCSTR buf)
{
	string256 tmp;
	int cnt = _GetItemCount(buf);
	R_ASSERT(cnt <= GAMEMTL_SUBITEM_COUNT);
	for (int k = 0; k < cnt; ++k)
		lst.push_back(_GetItem(buf, k, tmp));
}

SGameMtlPair::~SGameMtlPair()
{
	// destroy all media
	DestroySounds(BreakingSounds);
	DestroySounds(StepSounds);
	DestroySounds(CollideSounds);
	DestroyPSs(CollideParticles);
	// DestroyMarks (CollideMarks);
	//RenderFactory->DestroyGameMtlPair(m_pCollideMarks);
	//m_pCollideMarks->
}

void SGameMtlPair::Load(IReader& fs)
{
	shared_str buf;

	R_ASSERT(fs.find_chunk(GAMEMTLPAIR_CHUNK_PAIR));
	mtl0 = fs.r_u32();
	mtl1 = fs.r_u32();
	ID = fs.r_u32();
	ID_parent = fs.r_u32();
	OwnProps.assign(fs.r_u32());

	R_ASSERT(fs.find_chunk(GAMEMTLPAIR_CHUNK_BREAKING));
	fs.r_stringZ(buf);
	CreateSounds(BreakingSounds, *buf);

	R_ASSERT(fs.find_chunk(GAMEMTLPAIR_CHUNK_STEP));
	fs.r_stringZ(buf);
	CreateSounds(StepSounds, *buf);

	R_ASSERT(fs.find_chunk(GAMEMTLPAIR_CHUNK_COLLIDE));
	fs.r_stringZ(buf);
	CreateSounds(CollideSounds, *buf);
	fs.r_stringZ(buf);
	CreatePSs(CollideParticles, *buf);
	fs.r_stringZ(buf);
	//CreateMarks (CollideMarks,*buf);
	CreateMarks(&*m_pCollideMarks, *buf);
}
