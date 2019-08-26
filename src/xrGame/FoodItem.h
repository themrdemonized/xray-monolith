#pragma once

#include "eatable_item_object.h"
#include "script_export_space.h"

class CFoodItem : public CEatableItemObject
{
public:
	CFoodItem();
	virtual ~CFoodItem();
	
	DECLARE_SCRIPT_REGISTER_FUNCTION
};

add_to_type_list(CFoodItem)
#undef script_type_list
#define script_type_list save_type_list(CFoodItem)