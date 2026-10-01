#pragma once
namespace r1st {
inline float loadingWidth(float width,bool active,unsigned screenWidth,unsigned screenHeight){
 return active&&screenHeight&&screenWidth>=screenHeight*3&&screenWidth<=screenHeight*4 ? width*.5f : width;
}
}
