#pragma once

class CBlender_pp_bloom : public IBlender
{
public:
    virtual		LPCSTR		getComment()	{ return "Nice bloom bro!"; }
    virtual		BOOL		canBeDetailed()	{ return FALSE; }
    virtual		BOOL		canBeLMAPped()	{ return FALSE; }

    virtual		void		Compile(CBlender_Compile& C);

    CBlender_pp_bloom();
    virtual ~CBlender_pp_bloom();
}; 