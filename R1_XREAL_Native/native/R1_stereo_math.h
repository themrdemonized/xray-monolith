#pragma once
#include <stdexcept>
#include <cmath>
namespace r1st {
struct V {float x,y,z;};
struct Camera {V eye,right,up,forward;float fov,aspect,near_plane,far_plane;};
struct Eye {V eye,right,up,forward;float sx,sy,shift;};
struct Pair {Eye left,right;};
inline float dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline V add(V a,V b,float scale=1){return {a.x+b.x*scale,a.y+b.y*scale,a.z+b.z*scale};}
inline bool finite(V a){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
inline Pair make_pair(const Camera& c,float ipd,float convergence){
    if(!finite(c.eye)||!finite(c.right)||!finite(c.up)||!finite(c.forward)||
       !std::isfinite(ipd)||ipd<0||ipd>.085f||!std::isfinite(convergence)||convergence<.5f||
       !std::isfinite(c.fov)||c.fov<10||c.fov>150||!std::isfinite(c.aspect)||c.aspect<=0||
       !std::isfinite(c.near_plane)||!std::isfinite(c.far_plane)||c.near_plane<=0||c.far_plane<=c.near_plane||
       std::abs(dot(c.right,c.right)-1)>.001f||std::abs(dot(c.up,c.up)-1)>.001f||std::abs(dot(c.forward,c.forward)-1)>.001f||
       std::abs(dot(c.right,c.up))>.001f||std::abs(dot(c.right,c.forward))>.001f||std::abs(dot(c.up,c.forward))>.001f)
        throw std::invalid_argument("Invalid stereo camera");
    float sy=1/std::tan(c.fov*3.14159265358979323846f/360.f),sx=sy/c.aspect;
    Eye right{c.eye,c.right,c.up,c.forward,sx,sy,0};
    Eye left=right;left.eye=add(c.eye,c.right,-ipd);left.shift=-ipd*sx/convergence;
    return {left,right};
}
inline V project(const Eye& e,V point){
    V relative=add(point,e.eye,-1);float z=dot(relative,e.forward);
    if(!std::isfinite(z)||z<=0)throw std::invalid_argument("Point is behind the eye");
    return {dot(relative,e.right)*e.sx/z+e.shift,dot(relative,e.up)*e.sy/z,z};
}
}
