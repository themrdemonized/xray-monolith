#pragma once

class CBlender_hdr10_lens_flare_downsample : public IBlender
{
public:
    virtual		LPCSTR		getComment()	{ return "HDR10 Lens Flare Downsample"; }
    virtual		BOOL		canBeDetailed()	{ return FALSE; }
    virtual		BOOL		canBeLMAPped()	{ return FALSE; }

    virtual		void		Compile(CBlender_Compile& C);

    CBlender_hdr10_lens_flare_downsample();
    virtual ~CBlender_hdr10_lens_flare_downsample();
};

class CBlender_hdr10_lens_flare_fgen : public IBlender
{
public:
    virtual		LPCSTR		getComment()	{ return "HDR10 Lens Flare Feature Generation"; }
    virtual		BOOL		canBeDetailed()	{ return FALSE; }
    virtual		BOOL		canBeLMAPped()	{ return FALSE; }

    virtual		void		Compile(CBlender_Compile& C);

    CBlender_hdr10_lens_flare_fgen();
    virtual ~CBlender_hdr10_lens_flare_fgen();
};

class CBlender_hdr10_lens_flare_blur : public IBlender
{
public:
    virtual		LPCSTR		getComment()	{ return "HDR10 Lens Flare Blur"; }
    virtual		BOOL		canBeDetailed()	{ return FALSE; }
    virtual		BOOL		canBeLMAPped()	{ return FALSE; }

    virtual		void		Compile(CBlender_Compile& C);

    CBlender_hdr10_lens_flare_blur();
    virtual ~CBlender_hdr10_lens_flare_blur();
};

class CBlender_hdr10_lens_flare_upsample : public IBlender
{
public:
    virtual		LPCSTR		getComment()	{ return "HDR10 Lens Flare Upsample"; }
    virtual		BOOL		canBeDetailed()	{ return FALSE; }
    virtual		BOOL		canBeLMAPped()	{ return FALSE; }

    virtual		void		Compile(CBlender_Compile& C);

    CBlender_hdr10_lens_flare_upsample();
    virtual ~CBlender_hdr10_lens_flare_upsample();
};


