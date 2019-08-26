#pragma once
#include "script_export_space.h"
#include "script_light_inline.h"

typedef class_exporter<ScriptLight> CScriptLight;
add_to_type_list(CScriptLight)
#undef script_type_list
#define script_type_list save_type_list(CScriptLight)