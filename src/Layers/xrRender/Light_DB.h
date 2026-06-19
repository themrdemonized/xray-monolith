#pragma once

#include "light.h"
#include "light_package.h"

class CLight_DB
{
private:
	xr_vector<ref_light> v_static;
	xr_vector<ref_light> v_hemi;
public:
	ref_light sun_original;
	ref_light sun_adapted;
	xr_vector<ref_light> sun_cascades; // pip: one sun light per cascade
	ref_light rain_light;              // pip: persistent rain shadow light
	light_Package package;
public:
	void add_light(light* L);

	void Load(IReader* fs);
#if RENDER != R_R1
	void					LoadHemi			();
#endif
	void Unload();

	light* Create();
	void Update();

	CLight_DB();
	~CLight_DB();
};
