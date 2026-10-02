#pragma once

#include "../xrRender/blenders/Blender.h"

class CBlender_temporal_prepare final : public IBlender
{
public:
    CBlender_temporal_prepare();
    ~CBlender_temporal_prepare() override = default;

    LPCSTR getComment() override { return "Temporal AA preparation"; }
    BOOL canBeLMAPped() override { return FALSE; }
    void Compile(CBlender_Compile& C) override;
};
