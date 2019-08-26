///////////////////////////////////////////////////////////////
// Antirad.h
// Antirad - таблетки выводящие радиацию
///////////////////////////////////////////////////////////////


#pragma once

#include "eatable_item_object.h"
#include "script_export_space.h"

class CAntirad: public CEatableItemObject {

public:
	CAntirad();
	virtual ~CAntirad();

	DECLARE_SCRIPT_REGISTER_FUNCTION
};

add_to_type_list(CAntirad)
#undef script_type_list
#define script_type_list save_type_list(CAntirad)