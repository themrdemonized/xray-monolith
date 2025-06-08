////////////////////////////////////////////////////////////////////////////
//	Module 		: script_engine_inline.h
//	Created 	: 01.04.2004
//  Modified 	: 01.04.2004
//	Author		: Dmitriy Iassenev
//	Description : XRay Script Engine inline functions
////////////////////////////////////////////////////////////////////////////

#pragma once

template <typename _result_type>
IC bool CScriptEngine::functor(LPCSTR function_to_call, luabind::functor<_result_type>& lua_function)
{
	luabind::object object;
	if (!function_object(function_to_call, object))
		return (false);

	try
	{
		lua_function = luabind::object_cast<luabind::functor<_result_type>>(object);
	}
	catch (...)
	{
		return (false);
	}

	return (true);
}
