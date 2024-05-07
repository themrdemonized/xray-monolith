////////////////////////////////////////////////////////////////////////////
//	Module 		: script_ini_file_script.cpp
//	Created 	: 25.06.2004
//  Modified 	: 25.06.2004
//	Author		: Dmitriy Iassenev
//	Description : Script ini file class export
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "script_ini_file.h"

#include "string_table.h"
#include "script_engine.h"
#include "ai_space.h"

using namespace luabind;

CScriptIniFile* get_system_ini()
{
	return ((CScriptIniFile*)pSettings);
}

//Alundaio: The extended ability to reload system ini after application launch
#ifdef INI_FILE_EXTENDED_EXPORTS
CScriptIniFile* reload_system_ini()
{
	pSettings->Destroy(const_cast<CInifile*>(pSettings));
	string_path fname;
	FS.update_path(fname, "$game_config$", "system.ltx");
	pSettings = xr_new<CInifile>(fname);
	return ((CScriptIniFile*)pSettings);
}

void section_for_each(CScriptIniFile* self, luabind::functor<bool> functor)
{
	typedef CInifile::Root sections_type;
	sections_type& sections = self->sections();

	sections_type::const_iterator i = sections.begin();
	sections_type::const_iterator e = sections.end();
	for (; i != e; ++i)
	{
		if (functor((LPCSTR)(*i)->Name.c_str()) == true)
			return;
	}
}
#endif
//Alundaio: END

#ifdef XRGAME_EXPORTS
CScriptIniFile* get_game_ini()
{
	return ((CScriptIniFile*)pGameIni);
}
#endif // XRGAME_EXPORTS

bool r_line(CScriptIniFile* self, LPCSTR S, int L, luabind::internal_string&N, luabind::internal_string& V)
{
	THROW3(self->section_exist(S), "Cannot find section", S);
	THROW2((int)self->line_count(S) > L, "Invalid line number");

	N = "";
	V = "";

	LPCSTR n, v;
	bool result = !!self->r_line(S, L, &n, &v);
	if (!result)
		return (false);

	N = n;
	if (v)
		V = v;
	return (true);
}

#pragma warning(push)
#pragma warning(disable:4238)
CScriptIniFile* create_ini_file(LPCSTR ini_string)
{
	return (
		(CScriptIniFile*)
		xr_new<CInifile>(
			&IReader(
				(void*)ini_string,
				xr_strlen(ini_string)
			),
			FS.get_path("$game_config$")->m_Path
		)
	);
}
#pragma warning(pop)

// demonized: get modded exes version
int get_modded_exes_version() {
	LPSTR month_id[12] =
	{
		"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};

	LPCSTR build_date = __DATE__;

	int days;
	int months = 0;
	int years;
	string16 month;
	string256 buffer;
	xr_strcpy(buffer, build_date);
	sscanf(buffer, "%s %d %d", month, &days, &years);

	for (int i = 0; i < 12; i++)
	{
		if (_stricmp(month_id[i], month))
			continue;

		months = i;
		break;
	}

	months++;

	// Convert the parsed date to the desired integer format
	int result = years * 10000 + months * 100 + days;
	return result;
}

luabind::object get_string_table() {
	auto pData = CStringTable::getPData();
	luabind::object table = luabind::newtable(ai().script_engine().lua());
	if (!pData) {
		return table;
	}

	for (const auto& v : pData->m_StringTable) {
		table[v.first.c_str()] = v.second.c_str();
	}
	
	return table;
}

#pragma optimize("s",on)
void CScriptIniFile::script_register(lua_State* L)
{
	module(L)
	[
		class_<CScriptIniFile>("ini_file")
		.def(constructor<LPCSTR>())
		//Alundaio: Extend script ini file
#ifdef INI_FILE_EXTENDED_EXPORTS
		.def(constructor<IReader*, LPCSTR>())
		.def(constructor<LPCSTR, BOOL, BOOL, BOOL, LPCSTR>())
		.def("w_bool", &CScriptIniFile::w_bool)
		.def("w_color", &CScriptIniFile::w_color)
		.def("w_fcolor", &CScriptIniFile::w_fcolor)
		.def("w_float", &CScriptIniFile::w_float)
		.def("w_fvector2", &CScriptIniFile::w_fvector2)
		.def("w_fvector3", &CScriptIniFile::w_fvector3)
		.def("w_fvector4", &CScriptIniFile::w_fvector4)
		.def("w_s16", &CScriptIniFile::w_s16)
		.def("w_s32", &CScriptIniFile::w_s32)
		.def("w_s64", &CScriptIniFile::w_s64)
		.def("w_s8", &CScriptIniFile::w_s8)
		.def("w_string", &CScriptIniFile::w_string)
		.def("w_u16", &CScriptIniFile::w_u16)
		.def("w_u32", &CScriptIniFile::w_u32)
		.def("w_u64", &CScriptIniFile::w_u64)
		.def("w_u8", &CScriptIniFile::w_u8)
		.def("save_as", &CScriptIniFile::save_as)
		.def("save_at_end", &CScriptIniFile::save_at_end)
		.def("remove_line", &CScriptIniFile::remove_line)
		.def("set_override_names", &CScriptIniFile::set_override_names)
		.def("section_count", &CScriptIniFile::section_count)
		.def("section_for_each", &::section_for_each)
		.def("set_readonly", &CScriptIniFile::set_readonly)
#endif
		//Alundaio: END
		.def("section_exist", &CScriptIniFile::section_exist)
		.def("line_exist", &CScriptIniFile::line_exist)
		.def("r_clsid", &CScriptIniFile::r_clsid)
		.def("r_bool", &CScriptIniFile::r_bool)
		.def("r_token", &CScriptIniFile::r_token)
		.def("r_string_wq", &CScriptIniFile::r_string_wb)
		.def("line_count", &CScriptIniFile::line_count)
		.def("r_string", &CScriptIniFile::r_string)
		.def("r_u32", &CScriptIniFile::r_u32)
		.def("r_s32", &CScriptIniFile::r_s32)
		.def("r_float", &CScriptIniFile::r_float)
		.def("r_vector", &CScriptIniFile::r_fvector3)
		.def("close", &CScriptIniFile::close)
		.def("r_line", &::r_line, out_value(_4) + out_value(_5))

		// demonized: new exports
		.def("get_filename", &CScriptIniFile::fname)
		.def("dltx_print", &CScriptIniFile::DLTX_print)
		.def("dltx_get_filename_of_line", &CScriptIniFile::DLTX_getFilenameOfLine)
		.def("dltx_get_section", &CScriptIniFile::DLTX_scriptGetSection)
		.def("dltx_is_override", &CScriptIniFile::DLTX_isOverride),

		def("system_ini", &get_system_ini),
		//Alundaio: extend
#ifdef INI_FILE_EXTENDED_EXPORTS
		def("reload_system_ini", &reload_system_ini),
#endif
		//Alundaio:: END
#ifdef XRGAME_EXPORTS
		def("game_ini", &get_game_ini),
#endif // XRGAME_EXPORTS
		def("create_ini_file", &create_ini_file, adopt(result)),

		// demonized: get modded exes version
		def("get_modded_exes_version", &get_modded_exes_version),

		// demonized: get translation strings table
		def("get_string_table", &get_string_table)
	];
}
