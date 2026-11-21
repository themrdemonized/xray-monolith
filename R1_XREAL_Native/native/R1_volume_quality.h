#pragma once
#include <cmath>
namespace r1st {
inline bool validVolumeCap(float cap){return std::isfinite(cap)&&(cap==0.f||(cap>=1.f&&cap<=5.f));}
// September accum_volumetric recalculates both slice spacing and energy from z.
// Change only this call; never persist the user's console setting or raise it.
struct VolumeQuality {
 float& value;const float saved;
 VolumeQuality(float& q,bool stereo,float method,float cap):value(q),saved(q){
  if(stereo&&method>0.f&&validVolumeCap(cap)&&cap>0.f&&std::isfinite(q)&&q>cap)value=cap;
 }
 ~VolumeQuality(){value=saved;}
 VolumeQuality(const VolumeQuality&)=delete;
 VolumeQuality& operator=(const VolumeQuality&)=delete;
};
}
