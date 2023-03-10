#pragma once


class CBlender_nightvision : public IBlender
{
public:
	virtual LPCSTR getComment() { return "nightvision"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_nightvision();
	virtual ~CBlender_nightvision();
};

//crookr
class CBlender_fakescope : public IBlender
{
public:
	virtual LPCSTR getComment() { return "fakescope"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_fakescope();
	virtual ~CBlender_fakescope();
};

//--DSR-- HeatVision_start
class CBlender_heatvision : public IBlender
{
public:
	virtual LPCSTR getComment() { return "heatvision"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_heatvision();
	virtual ~CBlender_heatvision();
};
//--DSR-- HeatVision_end
