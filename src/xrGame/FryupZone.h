#pragma once

#include "script_object.h"

class CFryupZone : public CScriptObject
{
	typedef CScriptObject inherited;

public:
	CFryupZone();
	virtual ~CFryupZone();

#if defined(DEBUG) || defined(DEBUG_DRAW)
	virtual void	OnRender				( );
#endif
};
