#pragma once


class CBlender_gasmask_drops : public IBlender
{
public:
	virtual LPCSTR getComment() { return "Gasmask_drops"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_gasmask_drops();
	virtual ~CBlender_gasmask_drops();
};
