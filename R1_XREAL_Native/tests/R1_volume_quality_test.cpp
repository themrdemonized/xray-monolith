#include "../native/R1_volume_quality.h"
#include <cassert>
#include <limits>
#include <cstdio>
int main(){
 float quality=3;
 {r1st::VolumeQuality guard(quality,true,1,1.5f);assert(quality==1.5f);}
 assert(quality==3);
 {r1st::VolumeQuality guard(quality,false,1,1.5f);assert(quality==3);}
 {r1st::VolumeQuality guard(quality,true,0,1.5f);assert(quality==3);}
 {r1st::VolumeQuality guard(quality,true,1,0);assert(quality==3);}
 quality=1;
 {r1st::VolumeQuality guard(quality,true,1,1.5f);assert(quality==1);}
 quality=3;
 try {r1st::VolumeQuality guard(quality,true,1,1.5f);throw 1;} catch(int){}
 assert(quality==3);
 assert(!r1st::validVolumeCap(std::numeric_limits<float>::quiet_NaN()));
 assert(!r1st::validVolumeCap(.5f));assert(!r1st::validVolumeCap(6));
 assert(r1st::validVolumeCap(0));assert(r1st::validVolumeCap(1));assert(r1st::validVolumeCap(5));
 puts("PASS stereo-only volume cap, original/vanilla bypass, lower quality preserved, restoration, invalid settings");
}
