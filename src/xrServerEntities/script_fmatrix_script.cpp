////////////////////////////////////////////////////////////////////////////
//	Module 		: script_fmatrix_script.cpp
//	Created 	: 28.06.2004
//  Modified 	: 28.06.2004
//	Author		: Dmitriy Iassenev
//	Description : Script float matrix script export
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "script_fmatrix.h"

using namespace luabind;

Fvector get_matrix_hpb(Fmatrix* self)
{
	Fvector temp;
	self->getHPB(temp);
	return temp;
}

#pragma optimize("s",on)
void CScriptFmatrix::script_register(lua_State* L)
{
	module(L)
		[
			class_<Fmatrix>("matrix")
			.def_readwrite("i", &Fmatrix::i)
			.def_readwrite("_14_", &Fmatrix::_14_)
			.def_readwrite("j", &Fmatrix::j)
			.def_readwrite("_24_", &Fmatrix::_24_)
			.def_readwrite("k", &Fmatrix::k)
			.def_readwrite("_34_", &Fmatrix::_34_)
			.def_readwrite("c", &Fmatrix::c)
			.def_readwrite("_44_", &Fmatrix::_44_)
			.def(constructor<>())
			.def("set", (Fmatrix & (Fmatrix::*)(const Fmatrix&))(&Fmatrix::set), return_reference_to<1>())
			.def("set",
				(Fmatrix & (Fmatrix::*)(const Fvector&, const Fvector&, const Fvector&, const Fvector&))(&Fmatrix::set),
				return_reference_to<1>())
			.def("identity", &Fmatrix::identity, return_reference_to<1>())
			.def("mk_xform", &Fmatrix::mk_xform, return_reference_to<1>())
			.def("build_camera_dir", &Fmatrix::build_camera_dir, return_reference_to<1>())
			.def("build_projection", &Fmatrix::build_projection, return_reference_to<1>())
			.def("mulA_43", &Fmatrix::mulA_43, return_reference_to<1>())
			.def("mulA_44", &Fmatrix::mulA_44, return_reference_to<1>())
			.def("mulB_43", &Fmatrix::mulB_43, return_reference_to<1>())
			.def("mulB_44", &Fmatrix::mulB_44, return_reference_to<1>())
			.def("mul_43", &Fmatrix::mul_43, return_reference_to<1>())
			.def("translate", (Fmatrix & (Fmatrix::*)(float, float, float))(&Fmatrix::translate), return_reference_to<1>())
			.def("translate", (Fmatrix & (Fmatrix::*)(const Fvector&))(&Fmatrix::translate), return_reference_to<1>())
			.def("translate_add", (Fmatrix & (Fmatrix::*)(float, float, float))(&Fmatrix::translate_add), return_reference_to<1>())
			.def("translate_add", (Fmatrix & (Fmatrix::*)(const Fvector&))(&Fmatrix::translate_add), return_reference_to<1>())
			.def("translate_over", (Fmatrix & (Fmatrix::*)(float, float, float))(&Fmatrix::translate_over), return_reference_to<1>())
			.def("translate_over", (Fmatrix & (Fmatrix::*)(const Fvector&))(&Fmatrix::translate_over), return_reference_to<1>())
			.def("transpose", (Fmatrix & (Fmatrix::*)())(&Fmatrix::transpose), return_reference_to<1>())
			.def("transpose", (Fmatrix & (Fmatrix::*)(const Fmatrix&))(&Fmatrix::transpose), return_reference_to<1>())
			.def("transform", (void (Fmatrix::*)(Fvector&, const Fvector&) const)(&Fmatrix::transform))
			.def("transform", (void (Fmatrix::*)(Fvector&) const)(&Fmatrix::transform))
			.def("transform_tiny", (void (Fmatrix::*)(Fvector&, const Fvector&) const)(&Fmatrix::transform_tiny))
			.def("transform_tiny", (void (Fmatrix::*)(Fvector&) const)(&Fmatrix::transform_tiny))
			.def("transform_dir", (void (Fmatrix::*)(Fvector&, const Fvector&) const)(&Fmatrix::transform_dir))
			.def("transform_dir", (void (Fmatrix::*)(Fvector&) const)(&Fmatrix::transform_dir))
			.def("mul", (Fmatrix & (Fmatrix::*)(const Fmatrix&, const Fmatrix&))(&Fmatrix::mul), return_reference_to<1>())
			.def("mul", (Fmatrix & (Fmatrix::*)(const Fmatrix&, float))(&Fmatrix::mul), return_reference_to<1>())
			.def("mul", (Fmatrix & (Fmatrix::*)(float))(&Fmatrix::mul), return_reference_to<1>())
			.def("invert", (Fmatrix & (Fmatrix::*)())(&Fmatrix::invert), return_reference_to<1>())
			.def("invert", (Fmatrix & (Fmatrix::*)(const Fmatrix&))(&Fmatrix::invert), return_reference_to<1>())
			.def("invert_b", &Fmatrix::invert_b, return_reference_to<1>())
			.def("div", (Fmatrix & (Fmatrix::*)(const Fmatrix&, float))(&Fmatrix::div), return_reference_to<1>())
			.def("div", (Fmatrix & (Fmatrix::*)(float))(&Fmatrix::div), return_reference_to<1>())
			.def("scale", (Fmatrix & (Fmatrix::*)(float, float, float))(&Fmatrix::scale), return_reference_to<1>())
			.def("scale", (Fmatrix & (Fmatrix::*)(const Fvector&))(&Fmatrix::scale), return_reference_to<1>())
			.def("setHPB", (Fmatrix & (Fmatrix::*)(float, float, float))(&Fmatrix::setHPB), return_reference_to<1>())
			.def("setHPB", (Fmatrix & (Fmatrix::*)(const Fvector&))(&Fmatrix::setHPB), return_reference_to<1>())
			.def("setXYZ", (Fmatrix & (Fmatrix::*)(float, float, float))(&Fmatrix::setXYZ), return_reference_to<1>())
			.def("setXYZ", (Fmatrix & (Fmatrix::*)(const Fvector&))(&Fmatrix::setXYZ), return_reference_to<1>())
			.def("setXYZi", (Fmatrix & (Fmatrix::*)(float, float, float))(&Fmatrix::setXYZi), return_reference_to<1>())
			.def("setXYZi", (Fmatrix & (Fmatrix::*)(const Fvector&))(&Fmatrix::setXYZi), return_reference_to<1>())
			.def("rotateX", &Fmatrix::rotateX, return_reference_to<1>())
			.def("rotateY", &Fmatrix::rotateY, return_reference_to<1>())
			.def("rotateZ", &Fmatrix::rotateZ, return_reference_to<1>())
			.def("mapXYZ", &Fmatrix::mapXYZ, return_reference_to<1>())
			.def("mapXZY", &Fmatrix::mapXZY, return_reference_to<1>())
			.def("mapYXZ", &Fmatrix::mapYXZ, return_reference_to<1>())
			.def("mapYZX", &Fmatrix::mapYZX, return_reference_to<1>())
			.def("mapZXY", &Fmatrix::mapZXY, return_reference_to<1>())
			.def("mapZYX", &Fmatrix::mapZYX, return_reference_to<1>())
			.def("mirrorX_over", &Fmatrix::mirrorX_over, return_reference_to<1>())
			.def("mirrorX_add", &Fmatrix::mirrorX_add, return_reference_to<1>())
			.def("mirrorY", &Fmatrix::mirrorY, return_reference_to<1>())
			.def("mirrorY_over", &Fmatrix::mirrorY_over, return_reference_to<1>())
			.def("mirrorY_add", &Fmatrix::mirrorY_add, return_reference_to<1>())
			.def("mirrorZ", &Fmatrix::mirrorZ, return_reference_to<1>())
			.def("mirrorZ_over", &Fmatrix::mirrorZ_over, return_reference_to<1>())
			.def("mirrorZ_add", &Fmatrix::mirrorZ_add, return_reference_to<1>())
			.def("getHPB", &get_matrix_hpb)
			.def("hud_to_world", &Fmatrix::hud_to_world, return_reference_to<1>())
			.def("cam_to_world", &Fmatrix::cam_to_world, return_reference_to<1>())
			.def("world_to_hud", &Fmatrix::world_to_hud, return_reference_to<1>())
			.def("custom_to_world", &Fmatrix::custom_to_world, return_reference_to<1>()),

			class_<Fquaternion>("quaternion")
			.def(constructor<>())
			.def_readwrite("w", &Fquaternion::w)
			.def_readwrite("x", &Fquaternion::x)
			.def_readwrite("y", &Fquaternion::y)
			.def_readwrite("z", &Fquaternion::z)
			.def("set", (Fquaternion & (Fquaternion::*)(float, float, float, float))(&Fquaternion::set), return_reference_to<1>())
			.def("set", (Fquaternion & (Fquaternion::*)(const Fquaternion&))(&Fquaternion::set), return_reference_to<1>())
			.def("set", (Fquaternion & (Fquaternion::*)(const Fmatrix&))(&Fquaternion::set), return_reference_to<1>())
			.def("set", (Fquaternion & (Fquaternion::*)(const Fmatrix&))(&Fquaternion::set), return_reference_to<1>())
			.def("add", (Fquaternion & (Fquaternion::*)(const Fquaternion&))(&Fquaternion::add), return_reference_to<1>())
			.def("add", (Fquaternion & (Fquaternion::*)(const Fquaternion&, const Fquaternion&))(&Fquaternion::add), return_reference_to<1>())
			.def("sub", (Fquaternion & (Fquaternion::*)(const Fquaternion&))(&Fquaternion::sub), return_reference_to<1>())
			.def("sub", (Fquaternion & (Fquaternion::*)(const Fquaternion&, const Fquaternion&))(&Fquaternion::sub), return_reference_to<1>())
			.def("mul", (Fquaternion & (Fquaternion::*)(const Fquaternion&, const Fquaternion&))(&Fquaternion::mul), return_reference_to<1>())
			.def("is_valid", &Fquaternion::isValid)
			.def("is_unit", &Fquaternion::isUnit)
			.def("magnitude", &Fquaternion::magnitude)
			.def("normalize", &Fquaternion::normalize, return_reference_to<1>())
			.def("identity", &Fquaternion::identity, return_reference_to<1>())
			.def("inverse", (Fquaternion & (Fquaternion::*)())(&Fquaternion::inverse), return_reference_to<1>())
			.def("inverse", (Fquaternion & (Fquaternion::*)(const Fquaternion&))(&Fquaternion::inverse), return_reference_to<1>())
			.def("inverse_with_w", (Fquaternion & (Fquaternion::*)())(&Fquaternion::inverse_with_w), return_reference_to<1>())
			.def("inverse_with_w", (Fquaternion & (Fquaternion::*)(const Fquaternion&))(&Fquaternion::inverse_with_w), return_reference_to<1>())
			.def("rotation_yaw_pitch_roll", (Fquaternion & (Fquaternion::*)(float, float, float))(&Fquaternion::rotationYawPitchRoll), return_reference_to<1>())
			.def("rotation_yaw_pitch_roll", (Fquaternion & (Fquaternion::*)(const Fvector&))(&Fquaternion::rotationYawPitchRoll), return_reference_to<1>())
			.def("rotation", &Fquaternion::rotation, return_reference_to<1>())
			.def("slerp", &Fquaternion::slerp, return_reference_to<1>())
			//.def("ln", &Fquaternion::ln, return_reference_to<1>())
			.def("exp", &Fquaternion::exp, return_reference_to<1>())
			.def("get_axis_angle", &Fquaternion::get_axis_angle)
			.def("cmp", &Fquaternion::cmp)
		];
}
