#pragma once

#include "light.h"
#include "light_package.h"

class CLight_DB
{
private:
	xr_vector<ref_light> v_static;
	xr_vector<ref_light> v_hemi;
	bool m_prepared;
	void LoadDynamic(IReader& reader, bool publish);
#if RENDER != R_R1
	void LoadHemi(IReader& reader, bool publish);
#endif
public:
	ref_light sun_original;
	ref_light sun_adapted;
	light* rain_light;
public:
	void add_light(light* L);

	void Load(IReader* fs);
	void LoadPrepared(const xr_vector<u8>& dynamic, const xr_vector<u8>& hemi);
	void Prepare(const xr_vector<u8>& dynamic, const xr_vector<u8>& hemi);
	void CommitPrepared();
	void PrepareForCache();
#if RENDER != R_R1
	void					LoadHemi			();
#endif
	void Unload();
	void Suspend();
	void Resume();
	void Swap(CLight_DB& other);

	light* Create(bool publish = true);
	void Update();

	CLight_DB();
	~CLight_DB();
};
