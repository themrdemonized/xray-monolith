#pragma once


class CBlender_blur : public IBlender
{
public:
	virtual LPCSTR getComment() { return "Blur generation"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_blur();
	virtual ~CBlender_blur();
};

// SSS
class CBlender_ssfx_ssr : public IBlender
{
public:
	virtual LPCSTR getComment() { return "ssfx_ssr"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_ssfx_ssr();
	virtual ~CBlender_ssfx_ssr();
};

class CBlender_ssfx_volumetric_blur : public IBlender
{
public:
	virtual LPCSTR getComment() { return "ssfx_volumetric_blur"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_ssfx_volumetric_blur();
	virtual ~CBlender_ssfx_volumetric_blur();
};

class CBlender_ssfx_ao : public IBlender
{
public:
	virtual LPCSTR getComment() { return "ssfx_ao"; }
	virtual BOOL canBeDetailed() { return FALSE; }
	virtual BOOL canBeLMAPped() { return FALSE; }

	virtual void Compile(CBlender_Compile& C);

	CBlender_ssfx_ao();
	virtual ~CBlender_ssfx_ao();
};
