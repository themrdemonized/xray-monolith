#pragma once

//GTAO by doenitz
class CBlender_GTAO : public IBlender
{
public:
	virtual LPCSTR getComment() { return "GTAO pass"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_GTAO();
	virtual ~CBlender_GTAO();
};

