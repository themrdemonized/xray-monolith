#pragma once

class CBlender_hdr10_bloom_downsample : public IBlender
{
public:
    virtual		LPCSTR		getComment()	{ return "HDR10 Bloom Downsample"; }
    virtual		BOOL		canBeDetailed()	{ return FALSE; }
    virtual		BOOL		canBeLMAPped()	{ return FALSE; }

    virtual		void		Compile(CBlender_Compile& C);

    CBlender_hdr10_bloom_downsample();
    virtual ~CBlender_hdr10_bloom_downsample();
};

class CBlender_hdr10_bloom_blur : public IBlender
{
public:
    virtual		LPCSTR		getComment()	{ return "HDR10 Bloom Blur"; }
    virtual		BOOL		canBeDetailed()	{ return FALSE; }
    virtual		BOOL		canBeLMAPped()	{ return FALSE; }

    virtual		void		Compile(CBlender_Compile& C);

    CBlender_hdr10_bloom_blur();
    virtual ~CBlender_hdr10_bloom_blur();
};

class CBlender_hdr10_bloom_upsample : public IBlender
{
public:
    virtual		LPCSTR		getComment()	{ return "HDR10 Bloom Upsample"; }
    virtual		BOOL		canBeDetailed()	{ return FALSE; }
    virtual		BOOL		canBeLMAPped()	{ return FALSE; }

    virtual		void		Compile(CBlender_Compile& C);

    CBlender_hdr10_bloom_upsample();
    virtual ~CBlender_hdr10_bloom_upsample();
};


