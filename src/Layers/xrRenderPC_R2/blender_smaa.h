#pragma once


class CBlender_smaa : public IBlender
{
public:
	virtual LPCSTR getComment() { return "SMAA"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_smaa();
	virtual ~CBlender_smaa();
};
