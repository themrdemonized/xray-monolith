#include "stdafx.h"
#pragma hdrstop

#include "r_backend_hemi.h"

R_hemi::R_hemi()
{
	unmap();
}

void R_hemi::unmap()
{
	c_pos_faces = nullptr;
	c_neg_faces = nullptr;
	c_material = nullptr;

    for (int i = 0; i < 8; ++i)
	    c_model_param[i] = nullptr;
}

void R_hemi::set_pos_faces(float posx, float posy, float posz)
{
	if (c_pos_faces) RCache.set_c(c_pos_faces, posx, posy, posz, 0);
}

void R_hemi::set_neg_faces(float negx, float negy, float negz)
{
	if (c_neg_faces) RCache.set_c(c_neg_faces, negx, negy, negz, 0);
}

void R_hemi::set_material(float x, float y, float z, float w)
{
	if (c_material) RCache.set_c(c_material, x, y, z, w);
}

void R_hemi::set_model_params(Fvector4* c)
{
    for (int i = 0; i < 8; ++i)
        if (c_model_param[i]) RCache.set_c(c_model_param[i], c[i]);
}

static const Fvector4 vec4_zero = { 0.f, 0.f, 0.f, 0.f };

void R_hemi::zero_model_params()
{
    for (int i = 0; i < 8; ++i)
        if (c_model_param[i]) RCache.set_c(c_model_param[i], vec4_zero);
}
