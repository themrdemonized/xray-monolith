#include "stdafx.h"
#include "UICDkey.h"

#include "../RegistryFuncs.h"
#include "player_name_modifyer.h"

// -------------------------------------------------------------------------------------------------


void GetPlayerName_FromRegistry(char* name, u32 const name_size)
{
	//string256	new_name;
	//if (!ReadRegistry_StrValue(REGISTRY_VALUE_USERNAME, name))
	{
		name[0] = 0;
		//Msg( "! Player name registry key (%s) not found !", REGISTRY_VALUE_USERNAME );
		return;
	}
	//u32 const max_name_length	=	GP_UNIQUENICK_LEN - 1;
	//if ( xr_strlen(name) > max_name_length )
	//{
	//	name[max_name_length] = 0;
	//}
	//if ( xr_strlen(name) == 0 )
	//{
	//	//Msg( "! Player name in registry is empty! (%s)", REGISTRY_VALUE_USERNAME );
	//}
	//modify_player_name(name, new_name);
	//strncpy_s(name, name_size, new_name, max_name_length);
}

void WritePlayerName_ToRegistry(LPSTR name)
{
	//u32 const max_name_length	=	GP_UNIQUENICK_LEN - 1;
	//if ( xr_strlen(name) > max_name_length )
	//{
	//	name[max_name_length] = 0;
	//}
	//WriteRegistry_StrValue(REGISTRY_VALUE_USERNAME, name);
}
