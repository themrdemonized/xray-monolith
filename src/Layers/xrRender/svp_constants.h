#ifndef svp_constantsH
#define svp_constantsH
#pragma once

class CBlender_Compile;

// registers every PiP auto-bound shader constant with its binder, called once from
// CBlender_Compile::SetMapping, a name registered without its binder never binds
void RegisterSvpConstants(CBlender_Compile& dst);

// startup guard, logs the svp binder table health once and flags any null or duplicate
void ValidateSvpConstants();

#endif
