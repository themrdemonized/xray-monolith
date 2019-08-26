///////////////////////////////////////////////////////////////
// Medkit.h
// Medkit - аптечка, повышающая здоровье
///////////////////////////////////////////////////////////////


#pragma once

#include "eatable_item_object.h"
#include "script_export_space.h"

class CMedkit : public CEatableItemObject
{
public:
	CMedkit();
	virtual ~CMedkit();
	
	DECLARE_SCRIPT_REGISTER_FUNCTION
};

add_to_type_list(CMedkit)
#undef script_type_list
#define script_type_list save_type_list(CMedkit)