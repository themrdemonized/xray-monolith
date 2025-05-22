////////////////////////////////////////////////////////////////////////////
//	Module 		: script_engine_inline.h
//	Created 	: 01.04.2004
//  Modified 	: 01.04.2004
//	Author		: Dmitriy Iassenev
//	Description : XRay Script Engine inline functions
////////////////////////////////////////////////////////////////////////////

#pragma once

IC lua_State* CScriptEngine::lua()
{
	return (m_virtual_machine);
}

IC void CScriptEngine::current_thread(CScriptThread* thread)
{
	VERIFY((thread && !m_current_thread) || !thread);
	m_current_thread = thread;
}

IC CScriptThread* CScriptEngine::current_thread() const
{
	return (m_current_thread);
}

IC void CScriptEngine::add_script_process(const EScriptProcessors& process_id, CScriptProcess* script_process)
{
	//	CScriptProcessStorage::const_iterator	I = m_script_processes.find(process_id);
	//	VERIFY									(I == m_script_processes.end());
	m_script_processes.insert(std::make_pair(process_id, script_process));
}

CScriptProcess* CScriptEngine::script_process(const EScriptProcessors& process_id) const
{
	CScriptProcessStorage::const_iterator I = m_script_processes.find(process_id);
	if ((I != m_script_processes.end()))
		return ((*I).second);
	return (0);
}

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

#ifdef USE_DEBUGGER
#	ifndef USE_LUA_STUDIO
		IC CScriptDebugger *CScriptEngine::debugger	()
		{
			return			(m_scriptDebugger);
		}
#	else // ifndef USE_LUA_STUDIO
#	endif // ifndef USE_LUA_STUDIO
#endif // #ifdef USE_DEBUGGER
