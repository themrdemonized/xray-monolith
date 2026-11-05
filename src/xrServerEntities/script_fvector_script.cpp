////////////////////////////////////////////////////////////////////////////
//	Module 		: script_fvector_script.cpp
//	Created 	: 28.06.2004
//  Modified 	: 28.06.2004
//	Author		: Dmitriy Iassenev
//	Description : Script float vector script export
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "script_fvector.h"

using namespace luabind;

IC bool bounding_sphere_overlap(const Fvector& c0, float r0, const Fvector& c1, float r1)
{
    Fvector d; d.sub(c1, c0);
    float r = r0 + r1;
    return d.square_magnitude() <= r * r;
}

// Fobb vs. Fsphere
IC bool intersect(const Fobb& b, const Fsphere& s)
{
    float RB = _sqrt(b.m_halfsize.x * b.m_halfsize.x + b.m_halfsize.y * b.m_halfsize.y + b.m_halfsize.z * b.m_halfsize.z);

    // Early out if too far apart to collide
    if (!bounding_sphere_overlap(s.P, s.R, b.m_translate, RB))
        return false;

	Fvector d;
	d.sub(s.P, b.m_translate);

	float x = d.dotproduct(b.m_rotate.i);
	float y = d.dotproduct(b.m_rotate.j);
	float z = d.dotproduct(b.m_rotate.k);

	float dx = x - clampr(x, -b.m_halfsize.x, b.m_halfsize.x);
	float dy = y - clampr(y, -b.m_halfsize.y, b.m_halfsize.y);
	float dz = z - clampr(z, -b.m_halfsize.z, b.m_halfsize.z);

	return (dx * dx + dy * dy + dz * dz) <= (s.R * s.R);
}

IC bool intersect(const Fsphere& s, const Fobb& b)
{
	return intersect(b, s);
}

// FCylinder vs. FSphere
IC bool sphere_vs_cylinder_mantle(const Fsphere& s, const Fcylinder& c)
{
    Fvector axis = c.m_direction;
    axis.normalize();

    float hh = 0.5f * c.m_height;

    Fvector d; d.sub(s.P, c.m_center);
    float t = d.dotproduct(axis);

    if (t < -hh || t > hh)
        return false;

    Fvector closest = axis;
    closest.mul(t);
    closest.add(c.m_center);

    Fvector diff; diff.sub(s.P, closest);
    float r = c.m_radius + s.R;

    return diff.square_magnitude() <= r * r;
}

IC bool sphere_vs_disk(const Fsphere& s, const Fvector& diskC, const Fvector& diskN, float diskR)
{
    Fvector d; d.sub(s.P, diskC);
    float planeDist = d.dotproduct(diskN);

    if (fabsf(planeDist) > s.R)
        return false; // Not touching disk

    Fvector proj = d;
    proj.mad(diskN, -planeDist);

    float r = diskR + _sqrt(_max(0.f, s.R * s.R - planeDist * planeDist));
    return proj.square_magnitude() <= r * r;
}

IC bool intersect(const Fcylinder& c, const Fsphere& s)
{
    float RA = _sqrt(0.25f * c.m_height * c.m_height + c.m_radius * c.m_radius);

    // Early out if too far apart to collide
    if (!bounding_sphere_overlap(c.m_center, RA, s.P, s.R))
        return false;

    // Mantle vs. Sphere
    if (sphere_vs_cylinder_mantle(s, c))
        return true;

    // Caps vs. Sphere
    Fvector axis = c.m_direction;
    axis.normalize();

    float h = 0.5f * c.m_height;

    Fvector cap0 = axis; cap0.mul(-h); cap0.add(c.m_center);
    Fvector cap1 = axis; cap1.mul(h); cap1.add(c.m_center);

    if (sphere_vs_disk(s, cap0, axis, c.m_radius)) return true;
    if (sphere_vs_disk(s, cap1, axis, c.m_radius)) return true;

    return false;
}

IC bool intersect(const Fsphere& b, const Fcylinder& c)
{
    return intersect(c, b);
}

// FCylinder vs. Fcylinder
IC void project_cylinder(const Fcylinder& c, const Fvector& axis, float& minP, float& maxP)
{
    Fvector u = c.m_direction;
    u.normalize();

    float center = c.m_center.dotproduct(axis);

    // Caps
    float axial = 0.5f * c.m_height * fabsf(u.dotproduct(axis));

    // Mantle
    Fvector tmp;
    tmp.crossproduct(u, axis);
    float radial = c.m_radius * _sqrt(tmp.square_magnitude());

    float r = axial + radial;
    minP = center - r;
    maxP = center + r;
}

IC bool intersect(const Fcylinder& A, const Fcylinder& B)
{
    float RA = _sqrt(0.25f * A.m_height * A.m_height + A.m_radius * A.m_radius);
    float RB = _sqrt(0.25f * B.m_height * B.m_height + B.m_radius * B.m_radius);

    // Early out if too far apart to collide
    if (!bounding_sphere_overlap(A.m_center, RA, B.m_center, RB))
        return false;

    Fvector aAxis = A.m_direction;
    Fvector bAxis = B.m_direction;
    aAxis.normalize();
    bAxis.normalize();

    Fvector axes[6];
    int n = 0;

    // Caps
    axes[n++] = aAxis;
    axes[n++] = bAxis;

    // Mantles
    Fvector cross;
    cross.crossproduct(aAxis, bAxis);

    if (cross.square_magnitude() > 1e-6f)
    {
        axes[n++] = cross;

        // Cap A vs. Mantle B
        Fvector tmp;
        tmp.crossproduct(aAxis, cross);
        axes[n++] = tmp;

        // Cap B vs. Mantle A
        tmp.crossproduct(bAxis, cross);
        axes[n++] = tmp;
    }
    else
    {
        Fvector d; d.sub(B.m_center, A.m_center);
        Fvector radial = d;
        radial.mad(aAxis, -d.dotproduct(aAxis));

        if (radial.square_magnitude() > 1e-6f)
            axes[n++] = radial;
    }

    // SAT-Test
    for (int i = 0; i < n; ++i)
    {
        Fvector axis = axes[i];
        axis.normalize();

        float a0, a1, b0, b1;
        project_cylinder(A, axis, a0, a1);
        project_cylinder(B, axis, b0, b1);

        if (a1 < b0 || b1 < a0)
            return false;
    }

    return true;
}

// Fcylinder vs. Fobb
IC void project_cylinder_mantle(const Fcylinder& c, const Fvector& axis, float& minP, float& maxP)
{
    Fvector cylAxis = c.m_direction;
    cylAxis.normalize();

    float center = c.m_center.dotproduct(axis);

    float halfH = 0.5f * c.m_height * fabsf(cylAxis.dotproduct(axis));

    Fvector tmp;
    tmp.crossproduct(cylAxis, axis);
    float radial = c.m_radius * _sqrt(tmp.square_magnitude());

    float r = halfH + radial;
    minP = center - r;
    maxP = center + r;
}

IC void project_obb(const Fobb& b, const Fvector& axis, float& minP, float& maxP)
{
    float c = b.m_translate.dotproduct(axis);

    float r =
        fabsf(b.m_halfsize.x * b.m_rotate.i.dotproduct(axis)) +
        fabsf(b.m_halfsize.y * b.m_rotate.j.dotproduct(axis)) +
        fabsf(b.m_halfsize.z * b.m_rotate.k.dotproduct(axis));

    minP = c - r;
    maxP = c + r;
}

IC bool disk_vs_obb(const Fvector& diskC, const Fvector& diskN, float diskR, const Fobb& b)
{
    Fvector d; d.sub(b.m_translate, diskC);
    float centerDist = d.dotproduct(diskN);

    float proj =
        fabsf(b.m_halfsize.x * b.m_rotate.i.dotproduct(diskN)) +
        fabsf(b.m_halfsize.y * b.m_rotate.j.dotproduct(diskN)) +
        fabsf(b.m_halfsize.z * b.m_rotate.k.dotproduct(diskN));

    if (fabsf(centerDist) > proj)
        return false; // Not touching disk

    Fvector closest = b.m_translate;

    for (int i = 0; i < 3; ++i)
    {
        const Fvector& axis = (&b.m_rotate.i)[i];
        float h = (&b.m_halfsize.x)[i];

        float t = (closest.sub(diskC, closest).dotproduct(axis));
        t = clampr(t, -h, h);
        closest.mad(axis, t);
    }

    Fvector diff; diff.sub(closest, diskC);
    diff.mad(diskN, -diff.dotproduct(diskN));

    return diff.square_magnitude() <= diskR * diskR;
}

IC void cylinder_caps(const Fcylinder& c, Fvector& cap0, Fvector& cap1, Fvector& axis)
{
    axis = c.m_direction;
    axis.normalize();

    float h = 0.5f * c.m_height;
    cap0 = axis; cap0.mul(-h); cap0.add(c.m_center);
    cap1 = axis; cap1.mul(h); cap1.add(c.m_center);
}

IC bool intersect(const Fcylinder& c, const Fobb& b)
{
    float RA = _sqrt(0.25f * c.m_height * c.m_height + c.m_radius * c.m_radius);
    float RB = _sqrt(b.m_halfsize.x * b.m_halfsize.x + b.m_halfsize.y * b.m_halfsize.y + b.m_halfsize.z * b.m_halfsize.z);

    // Early out if too far apart to collide
    if (!bounding_sphere_overlap(c.m_center, RA, b.m_translate, RB))
        return false;

    // Mantle vs. Fobb (SAT)
    Fvector axes[7];
    int n = 0;

    Fvector cylAxis = c.m_direction;
    cylAxis.normalize();
    axes[n++] = cylAxis;

    axes[n++] = b.m_rotate.i;
    axes[n++] = b.m_rotate.j;
    axes[n++] = b.m_rotate.k;

    Fvector tmp;
    tmp.crossproduct(cylAxis, b.m_rotate.i);
    if (tmp.square_magnitude() > 1e-6f) axes[n++] = tmp;

    tmp.crossproduct(cylAxis, b.m_rotate.j);
    if (tmp.square_magnitude() > 1e-6f) axes[n++] = tmp;

    tmp.crossproduct(cylAxis, b.m_rotate.k);
    if (tmp.square_magnitude() > 1e-6f) axes[n++] = tmp;

    for (int i = 0; i < n; ++i)
    {
        Fvector axis = axes[i];
        axis.normalize();

        float c0, c1, b0, b1;
        project_cylinder_mantle(c, axis, c0, c1);
        project_obb(b, axis, b0, b1);

        if (c1 < b0 || b1 < c0)
            return false;
    }

    // Caps vs. Fobb
    Fvector cap0, cap1, nAxis;
    cylinder_caps(c, cap0, cap1, nAxis);

    if (disk_vs_obb(cap0, nAxis, c.m_radius, b)) return true;
    if (disk_vs_obb(cap1, nAxis, c.m_radius, b)) return true;

    return true;
}

IC bool intersect(const Fobb& b, const Fcylinder& c)
{
    return intersect(c, b);
}

#pragma optimize("s",on)
void CScriptFvector::script_register(lua_State* L)
{
	module(L)
	[
		class_<Fvector>("vector")
		// demonized: new exports of static functions, in Lua use like this: vector.generate_orthonormal_basis(a, b, c)
		.scope()[
			def("generate_orthonormal_basis", &Fvector::generate_orthonormal_basis),
			def("generate_orthonormal_basis_normalized", &Fvector::generate_orthonormal_basis_normalized)
		]
		.def_readwrite("x", &Fvector::x)
		.def_readwrite("y", &Fvector::y)
		.def_readwrite("z", &Fvector::z)
		.def(constructor<>())
		.def("set", (Fvector & (Fvector::*)(float, float, float))(&Fvector::set), return_reference_to<1>())
		.def("set", (Fvector & (Fvector::*)(const Fvector&))(&Fvector::set), return_reference_to<1>())
		.def("add", (Fvector & (Fvector::*)(float))(&Fvector::add), return_reference_to<1>())
		.def("add", (Fvector & (Fvector::*)(float, float, float))(&Fvector::add), return_reference_to<1>())
		.def("add", (Fvector & (Fvector::*)(const Fvector&))(&Fvector::add), return_reference_to<1>())
		.def("add", (Fvector & (Fvector::*)(const Fvector&, const Fvector&))(&Fvector::add), return_reference_to<1>())
		.def("add", (Fvector & (Fvector::*)(const Fvector&, float))(&Fvector::add), return_reference_to<1>())
		.def("sub", (Fvector & (Fvector::*)(float))(&Fvector::sub), return_reference_to<1>())
		.def("sub", (Fvector & (Fvector::*)(float, float, float))(&Fvector::sub), return_reference_to<1>())
		.def("sub", (Fvector & (Fvector::*)(const Fvector&))(&Fvector::sub), return_reference_to<1>())
		.def("sub", (Fvector & (Fvector::*)(const Fvector&, const Fvector&))(&Fvector::sub), return_reference_to<1>())
		.def("sub", (Fvector & (Fvector::*)(const Fvector&, float))(&Fvector::sub), return_reference_to<1>())
		.def("mul", (Fvector & (Fvector::*)(float))(&Fvector::mul), return_reference_to<1>())
		.def("mul", (Fvector & (Fvector::*)(float, float, float))(&Fvector::mul), return_reference_to<1>())
		.def("mul", (Fvector & (Fvector::*)(const Fvector&))(&Fvector::mul), return_reference_to<1>())
		.def("mul", (Fvector & (Fvector::*)(const Fvector&, const Fvector&))(&Fvector::mul), return_reference_to<1>())
		.def("mul", (Fvector & (Fvector::*)(const Fvector&, float))(&Fvector::mul), return_reference_to<1>())
		.def("div", (Fvector & (Fvector::*)(float))(&Fvector::div), return_reference_to<1>())
		.def("div", (Fvector & (Fvector::*)(float, float, float))(&Fvector::div), return_reference_to<1>())
		.def("div", (Fvector & (Fvector::*)(const Fvector&))(&Fvector::div), return_reference_to<1>())
		.def("div", (Fvector & (Fvector::*)(const Fvector&, const Fvector&))(&Fvector::div), return_reference_to<1>())
		.def("div", (Fvector & (Fvector::*)(const Fvector&, float))(&Fvector::div), return_reference_to<1>())
		.def("invert", (Fvector & (Fvector::*)())(&Fvector::invert), return_reference_to<1>())
		.def("invert", (Fvector & (Fvector::*)(const Fvector&))(&Fvector::invert), return_reference_to<1>())
		.def("min", (Fvector & (Fvector::*)(const Fvector&))(&Fvector::min), return_reference_to<1>())
		.def("min", (Fvector & (Fvector::*)(const Fvector&, const Fvector&))(&Fvector::min), return_reference_to<1>())
		.def("max", (Fvector & (Fvector::*)(const Fvector&))(&Fvector::max), return_reference_to<1>())
		.def("max", (Fvector & (Fvector::*)(const Fvector&, const Fvector&))(&Fvector::max), return_reference_to<1>())
		.def("abs", &Fvector::abs, return_reference_to<1>())
		.def("similar", &Fvector::similar)
		.def("set_length", &Fvector::set_length, return_reference_to<1>())
		.def("align", &Fvector::align, return_reference_to<1>())
		//			.def("squeeze",						&Fvector::squeeze,																										return_reference_to<1>())
		.def("clamp", (Fvector & (Fvector::*)(const Fvector&))(&Fvector::clamp), return_reference_to<1>())
		.def("clamp", (Fvector & (Fvector::*)(const Fvector&, const Fvector&))(&Fvector::clamp),
		     return_reference_to<1>())
		.def("inertion", &Fvector::inertion, return_reference_to<1>())
		.def("average", (Fvector & (Fvector::*)(const Fvector&))(&Fvector::average), return_reference_to<1>())
		.def("average", (Fvector & (Fvector::*)(const Fvector&, const Fvector&))(&Fvector::average),
		     return_reference_to<1>())
		.def("lerp", &Fvector::lerp, return_reference_to<1>())
		.def("mad", (Fvector & (Fvector::*)(const Fvector&, float))(&Fvector::mad), return_reference_to<1>())
		.def("mad", (Fvector & (Fvector::*)(const Fvector&, const Fvector&, float))(&Fvector::mad),
		     return_reference_to<1>())
		.def("mad", (Fvector & (Fvector::*)(const Fvector&, const Fvector&))(&Fvector::mad), return_reference_to<1>())
		.def("mad", (Fvector & (Fvector::*)(const Fvector&, const Fvector&, const Fvector&))(&Fvector::mad),
		     return_reference_to<1>())
		//			.def("square_magnitude",			&Fvector::square_magnitude)
		.def("magnitude", &Fvector::magnitude)
		//			.def("normalize_magnitude",			&Fvector::normalize_magn)
		.def("normalize", (Fvector & (Fvector::*)())(&Fvector::normalize_safe), return_reference_to<1>())
		.def("normalize", (Fvector & (Fvector::*)(const Fvector&))(&Fvector::normalize_safe), return_reference_to<1>())
		.def("normalize_safe", (Fvector & (Fvector::*)())(&Fvector::normalize_safe), return_reference_to<1>())
		.def("normalize_safe", (Fvector & (Fvector::*)(const Fvector&))(&Fvector::normalize_safe),
		     return_reference_to<1>())
		//			.def("random_dir",					(Fvector & (Fvector::*)())(&Fvector::random_dir),																		return_reference_to<1>())
		//			.def("random_dir",					(Fvector & (Fvector::*)(const Fvector &, float))(&Fvector::random_dir),													return_reference_to<1>())
		//			.def("random_point",				(Fvector & (Fvector::*)(const Fvector &))(&Fvector::random_point),														return_reference_to<1>())
		//			.def("random_point",				(Fvector & (Fvector::*)(float))(&Fvector::random_point),																return_reference_to<1>())
		.def("dotproduct", &Fvector::dotproduct)
		.def("crossproduct", &Fvector::crossproduct, return_reference_to<1>())
		.def("distance_to_xz", &Fvector::distance_to_xz)
		.def("distance_to_xz_sqr", &Fvector::distance_to_xz_sqr)
		.def("distance_to_sqr", &Fvector::distance_to_sqr)
		.def("distance_to", &Fvector::distance_to)
		//			.def("from_bary",					(Fvector & (Fvector::*)(const Fvector &, const Fvector &, const Fvector &, float, float, float))(&Fvector::from_bary),	return_reference_to<1>())
		//			.def("from_bary",					(Fvector & (Fvector::*)(const Fvector &, const Fvector &, const Fvector &, const Fvector &))(&Fvector::from_bary),		return_reference_to<1>())
		//			.def("from_bary4",					&Fvector::from_bary4,																									return_reference_to<1>())
		//			.def("mknormal_non_normalized",		&Fvector::mknormal_non_normalized,																						return_reference_to<1>())
		//			.def("mknormal",					&Fvector::mknormal,																										return_reference_to<1>())
		.def("setHP", &Fvector::setHP, return_reference_to<1>())
		//			.def("getHP",						&Fvector::getHP,																																	out_value<2>() + out_value<3>())
		.def("getH", &Fvector::getH)
		.def("getP", &Fvector::getP)

		.def("reflect", &Fvector::reflect, return_reference_to<1>())
		.def("slide", &Fvector::slide, return_reference_to<1>())

		// demonized: new exports
		.def("project", (Fvector & (Fvector::*)(const Fvector&, const Fvector&))(&Fvector::project), return_reference_to<1>())
		.def("project", (Fvector & (Fvector::*)(const Fvector&))(&Fvector::project), return_reference_to<1>())
		.def("hud_to_world", &Fvector::hud_to_world, return_reference_to<1>())
		.def("world_to_hud", &Fvector::world_to_hud, return_reference_to<1>())
		.def("hud_to_world_dir", &Fvector::hud_to_world_dir, return_reference_to<1>())
		.def("world_to_hud_dir", &Fvector::world_to_hud_dir, return_reference_to<1>()),

		class_<Fvector2>("vector2")
		.def_readwrite("x", &Fvector2::x)
		.def_readwrite("y", &Fvector2::y)
		.def(constructor<>())

		// demonized/PrivatePirate: new exports
		.def("normalize", (Fvector2 & (Fvector2::*)(void))(&Fvector2::normalize), return_reference_to<1>())

		.def("set", (Fvector2 & (Fvector2::*)(float, float))(&Fvector2::set), return_reference_to<1>())
		.def("set", (Fvector2& (Fvector2::*)(const Fvector2&))(&Fvector2::set), return_reference_to<1>())
		.def("abs", &Fvector2::abs, return_reference_to<1>())
		.def("min", (Fvector2& (Fvector2::*)(float, float))(&Fvector2::min), return_reference_to<1>())
		.def("min", (Fvector2& (Fvector2::*)(const Fvector2&))(&Fvector2::min), return_reference_to<1>())
		.def("max", (Fvector2& (Fvector2::*)(float, float))(&Fvector2::max), return_reference_to<1>())
		.def("max", (Fvector2& (Fvector2::*)(const Fvector2&))(&Fvector2::max), return_reference_to<1>())
		.def("add", (Fvector2& (Fvector2::*)(float))(&Fvector2::add), return_reference_to<1>())
		.def("add", (Fvector2& (Fvector2::*)(float, float))(&Fvector2::add), return_reference_to<1>())
		.def("add", (Fvector2& (Fvector2::*)(const Fvector2&))(&Fvector2::add), return_reference_to<1>())
		.def("add", (Fvector2& (Fvector2::*)(const Fvector2&, const Fvector2&))(&Fvector2::add), return_reference_to<1>())
		.def("add", (Fvector2& (Fvector2::*)(const Fvector2&, float))(&Fvector2::add), return_reference_to<1>())
		.def("sub", (Fvector2& (Fvector2::*)(float))(&Fvector2::sub), return_reference_to<1>())
		.def("sub", (Fvector2& (Fvector2::*)(float, float))(&Fvector2::sub), return_reference_to<1>())
		.def("sub", (Fvector2& (Fvector2::*)(const Fvector2&))(&Fvector2::sub), return_reference_to<1>())
		.def("sub", (Fvector2& (Fvector2::*)(const Fvector2&, float))(&Fvector2::sub), return_reference_to<1>())
		.def("sub", (Fvector2& (Fvector2::*)(const Fvector2&, const Fvector2&))(&Fvector2::sub), return_reference_to<1>())
		.def("mul", (Fvector2& (Fvector2::*)(float))(&Fvector2::mul), return_reference_to<1>())
		.def("mul", (Fvector2& (Fvector2::*)(const Fvector2&))(&Fvector2::mul), return_reference_to<1>())
		.def("div", (Fvector2& (Fvector2::*)(float))(&Fvector2::div), return_reference_to<1>())
		.def("div", (Fvector2& (Fvector2::*)(const Fvector2&))(&Fvector2::div), return_reference_to<1>())
		.def("mad", &Fvector2::mad, return_reference_to<1>())
		.def("rot90", &Fvector2::rot90, return_reference_to<1>())
		.def("crossproduct", &Fvector2::crossproduct)
		.def("dotproduct", &Fvector2::dotproduct)
		.def("distance_to", &Fvector2::distance_to)
		.def("magnitude", &Fvector2::magnitude)
		.def("similar", (bool (Fvector2::*)(const Fvector2&, float) const)(&Fvector2::similar))
		.def("averageA", &Fvector2::averageA, return_reference_to<1>())
		.def("averageG", &Fvector2::averageG, return_reference_to<1>())
		.def("getH_Y", &Fvector2::getH)
		.def("getH_X", &Fvector2::getH_X)
		,

		class_<Fvector4>("vector4")
		.def_readwrite("x", &Fvector4::x)
		.def_readwrite("y", &Fvector4::y)
		.def_readwrite("z", &Fvector4::z)
		.def_readwrite("w", &Fvector4::w)
		.def(constructor<>())
		.def("set", (Fvector4& (Fvector4::*)(float, float, float, float))(&Fvector4::set), return_reference_to<1>())
		.def("set", (Fvector4& (Fvector4::*)(const Fvector4&))(&Fvector4::set), return_reference_to<1>())
		.def("add", (Fvector4& (Fvector4::*)(float, float, float, float))(&Fvector4::add), return_reference_to<1>())
		.def("add", (Fvector4& (Fvector4::*)(const Fvector4&))(&Fvector4::add), return_reference_to<1>())
		.def("add", (Fvector4& (Fvector4::*)(float))(&Fvector4::add), return_reference_to<1>())
		.def("add", (Fvector4& (Fvector4::*)(const Fvector4&, const Fvector4&))(&Fvector4::add), return_reference_to<1>())
		.def("add", (Fvector4& (Fvector4::*)(const Fvector4&, float))(&Fvector4::add), return_reference_to<1>())
		.def("sub", (Fvector4& (Fvector4::*)(float, float, float, float))(&Fvector4::sub), return_reference_to<1>())
		.def("sub", (Fvector4& (Fvector4::*)(const Fvector4&))(&Fvector4::sub), return_reference_to<1>())
		.def("sub", (Fvector4& (Fvector4::*)(float))(&Fvector4::sub), return_reference_to<1>())
		.def("sub", (Fvector4& (Fvector4::*)(const Fvector4&, const Fvector4&))(&Fvector4::sub), return_reference_to<1>())
		.def("sub", (Fvector4& (Fvector4::*)(const Fvector4&, float))(&Fvector4::sub), return_reference_to<1>())
		.def("mul", (Fvector4& (Fvector4::*)(float, float, float, float))(&Fvector4::mul), return_reference_to<1>())
		.def("mul", (Fvector4& (Fvector4::*)(const Fvector4&))(&Fvector4::mul), return_reference_to<1>())
		.def("mul", (Fvector4& (Fvector4::*)(float))(&Fvector4::mul), return_reference_to<1>())
		.def("mul", (Fvector4& (Fvector4::*)(const Fvector4&, const Fvector4&))(&Fvector4::mul), return_reference_to<1>())
		.def("mul", (Fvector4& (Fvector4::*)(const Fvector4&, float))(&Fvector4::mul), return_reference_to<1>())
		.def("div", (Fvector4& (Fvector4::*)(float, float, float, float))(&Fvector4::div), return_reference_to<1>())
		.def("div", (Fvector4& (Fvector4::*)(const Fvector4&))(&Fvector4::div), return_reference_to<1>())
		.def("div", (Fvector4& (Fvector4::*)(float))(&Fvector4::div), return_reference_to<1>())
		.def("div", (Fvector4& (Fvector4::*)(const Fvector4&, const Fvector4&))(&Fvector4::div), return_reference_to<1>())
		.def("div", (Fvector4& (Fvector4::*)(const Fvector4&, float))(&Fvector4::div), return_reference_to<1>())
		.def("clamp", (Fvector4& (Fvector4::*)(const Fvector4&, const Fvector4&))(&Fvector4::clamp), return_reference_to<1>())
		.def("clamp", (Fvector4& (Fvector4::*)(const Fvector4&))(&Fvector4::clamp), return_reference_to<1>())
		.def("similar", &Fvector4::similar)
		.def("magnitude", &Fvector4::magnitude)
		.def("normalize", &Fvector4::normalize, return_reference_to<1>())
		.def("normalize_as_plane", &Fvector4::normalize_as_plane, return_reference_to<1>())
		.def("lerp", &Fvector4::lerp, return_reference_to<1>()),

		class_<Fbox>("fbox")
		.def(constructor<>())
		.def_readwrite("x1", &Fbox::x1)
		.def_readwrite("x2", &Fbox::x2)
		.def_readwrite("y1", &Fbox::y1)
		.def_readwrite("y2", &Fbox::y2)
		.def_readwrite("z1", &Fbox::z1)
		.def_readwrite("z2", &Fbox::z2)
		.def_readwrite("min", &Fbox::min)
		.def_readwrite("max", &Fbox::max)
		.def("is_valid", &Fbox::is_valid)
		.def("set", (Fbox& (Fbox::*)(const Fvector&, const Fvector&))(&Fbox::set), return_reference_to<1>())
		.def("set", (Fbox& (Fbox::*)(float, float, float, float, float, float))(&Fbox::set), return_reference_to<1>())
		.def("set", (Fbox& (Fbox::*)(const Fbox&))(&Fbox::set), return_reference_to<1>())
		.def("setb", (Fbox& (Fbox::*)(const Fvector&, const Fvector&))(&Fbox::setb), return_reference_to<1>())
		.def("null", &Fbox::null, return_reference_to<1>())
		.def("identity", &Fbox::identity, return_reference_to<1>())
		.def("invalidate", &Fbox::invalidate, return_reference_to<1>())
		.def("shrink", (Fbox& (Fbox::*)(float))(&Fbox::shrink), return_reference_to<1>())
		.def("shrink", (Fbox& (Fbox::*)(const Fvector&))(&Fbox::shrink), return_reference_to<1>())
		.def("grow", (Fbox& (Fbox::*)(float))(&Fbox::grow), return_reference_to<1>())
		.def("grow", (Fbox& (Fbox::*)(const Fvector&))(&Fbox::grow), return_reference_to<1>())
		.def("add", (Fbox& (Fbox::*)(const Fvector&))(&Fbox::add), return_reference_to<1>())
		.def("sub", (Fbox& (Fbox::*)(const Fvector&))(&Fbox::sub), return_reference_to<1>())
		.def("offset", (Fbox& (Fbox::*)(const Fvector&))(&Fbox::offset), return_reference_to<1>())
		.def("add", (Fbox& (Fbox::*)(const Fbox&, const Fvector&))(&Fbox::add), return_reference_to<1>())
		.def("contains", static_cast<bool (Fbox::*)(float, float, float) const>(&Fbox::contains))
		.def("contains", static_cast<bool (Fbox::*)(const Fvector&) const>(&Fbox::contains))
		.def("contains", static_cast<bool (Fbox::*)(const Fbox&) const>(&Fbox::contains))
		.def("similar", (bool (Fbox::*)(const Fbox&))(&Fbox::similar))
		.def("modify", (Fbox& (Fbox::*)(const Fvector&))(&Fbox::modify), return_reference_to<1>())
		.def("modify", (Fbox& (Fbox::*)(float, float, float))(&Fbox::modify), return_reference_to<1>())
		.def("modify", (Fbox& (Fbox::*)(const Fbox&, const Fmatrix&))(&Fbox::modify), return_reference_to<1>())
		.def("merge", (Fbox& (Fbox::*)(const Fbox&))(&Fbox::merge), return_reference_to<1>())
		.def("merge", (Fbox& (Fbox::*)(const Fbox&, const Fbox&))(&Fbox::merge), return_reference_to<1>())
		.def("xform", (Fbox& (Fbox::*)(const Fbox&, const Fmatrix&))(&Fbox::xform), return_reference_to<1>())
		.def("xform", (Fbox& (Fbox::*)(const Fmatrix&))(&Fbox::xform), return_reference_to<1>())
		.def("getsize", static_cast<void (Fbox::*)(Fvector&) const>(&Fbox::getsize))
		.def("getradius", static_cast<void (Fbox::*)(Fvector&) const>(&Fbox::getradius))
		.def("getradius", static_cast<float (Fbox::*)() const>(&Fbox::getradius))
		.def("getvolume", static_cast<float (Fbox::*)() const>(&Fbox::getvolume))
		.def("getcenter", static_cast<const Fbox& (Fbox::*)(Fvector&) const>(&Fbox::getcenter), return_reference_to<1>())
		.def("get_CD", static_cast<const Fbox& (Fbox::*)(Fvector&, Fvector&) const>(&Fbox::get_CD), return_reference_to<1>())
		.def("scale", &Fbox::scale, return_reference_to<1>())
		.def("getsphere", &Fbox::getsphere, return_reference_to<1>())
		.def("intersect", &Fbox::intersect)
		.def("pick", &Fbox::Pick)
		.def("pick2", &Fbox::Pick2)
		.def("getpoint", &Fbox::getpoint),

		class_<Frect>("Frect")
		.def(constructor<>())
		.def("set", (Frect & (Frect::*)(float, float, float, float))(&Frect::set), return_reference_to<1>())
		.def_readwrite("lt", &Frect::lt)
		.def_readwrite("rb", &Frect::rb)
		.def_readwrite("x1", &Frect::x1)
		.def_readwrite("x2", &Frect::x2)
		.def_readwrite("y1", &Frect::y1)
		.def_readwrite("y2", &Frect::y2),

		class_<Fobb>("Fobb")
		.def(constructor<>())
		.def("intersect_box", static_cast<bool (Fobb::*)(const Fobb&) const>(&Fobb::intersect))
		.def("intersect_sphere", (bool (*)(const Fobb&, const Fsphere&))(&intersect))
		.def("intersect_cylinder", (bool (*)(const Fobb&, const Fcylinder&))(&intersect))
		.def_readonly("translate", &Fobb::m_translate)
		.def_readonly("halfsize", &Fobb::m_halfsize),

		class_<Fsphere>("Fsphere")
		.def(constructor<>())
		.def("intersect_box", (bool (*)(const Fsphere&, const Fobb&))(&intersect))
		.def("intersect_sphere", static_cast<bool (Fsphere::*)(const Fsphere&) const>(&Fsphere::intersect))
        .def("intersect_cylinder", (bool (*)(const Fsphere&, const Fcylinder&))(&intersect))
		.def_readonly("pos", &Fsphere::P)
		.def_readonly("radius", &Fsphere::R),

		class_<Fcylinder>("Fcylinder")
		.def(constructor<>())
        .def("intersect_box", (bool (*)(const Fcylinder&, const Fobb&))(&intersect))
        .def("intersect_sphere", (bool (*)(const Fcylinder&, const Fsphere&))(&intersect))
        .def("intersect_cylinder", (bool (*)(const Fcylinder&, const Fcylinder&))(&intersect))
        .def_readonly("center", &Fcylinder::m_center)
        .def_readonly("direction", &Fcylinder::m_direction)
        .def_readonly("height", &Fcylinder::m_height)
        .def_readonly("radius", &Fcylinder::m_radius)
	];
}
