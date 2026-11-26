#include "../native/R1_stereo_math.h"
#include "../native/R1_decompression.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace r1st;
void check(bool ok,const char* message){if(!ok){std::fprintf(stderr,"FAIL %s\n",message);std::exit(1);}}
bool near(float a,float b){return std::abs(a-b)<0.00001f;}
int main(){
    // Round-trip actual off-centre clip projection through the shader's pixel/depth reconstruction.
    for(float shift:{-.15f,0.f,.15f})for(float depth:{1.f,2.f,20.f}){
        const float sx=1.2f,sy=1.8f,x=.3f,y=.2f,width=3840.f,height=1080.f;
        float params[]={1/sx,-1/sy,2/(sx*width),-2/(sy*height)};
        r1st::correct_decompression(params,sx,sy,shift,0.f);
        const float px=(x*sx/depth+shift+1)*width/2;
        const float py=(1-y*sy/depth)*height/2;
        check(near(depth*(px*params[2]-params[0]),x),"off-centre depth reconstruct X");
        check(near(depth*(py*params[3]-params[1]),y),"depth reconstruct Y");
    }
    // Catches mirrored eyes, altered right-eye aim and incorrect convergence.
    Camera c{{0,0,0},{1,0,0},{0,1,0},{0,0,1},90,16.f/9.f,.08f,500};
    auto pair=make_pair(c,.064f,2);
    check(near(pair.right.eye.x,0),"right eye must preserve the aiming camera");
    check(near(pair.left.eye.x,-.064f),"left eye must lie 64mm left");
    auto r=project(pair.right,{0,0,2}); auto l=project(pair.left,{0,0,2});
    check(near(r.x,0)&&near(l.x,0),"target on convergence plane must coincide");
    auto closeL=project(pair.left,{0,0,1});
    auto farL=project(pair.left,{0,0,4});
    check(closeL.x>0&&farL.x<0,"near and far disparity must have opposite signs");
    check(near(project(pair.left,{0,1,2}).y,project(pair.right,{0,1,2}).y),"no vertical disparity");
    c.eye={5,2,3};c.right={0,0,-1};c.forward={1,0,0};
    pair=make_pair(c,.064f,2);
    check(near(pair.left.eye.z,3.064f)&&near(pair.left.eye.x,5),"IPD must follow camera rotation");
    check(near(project(pair.right,{7,2,3}).x,0),"rotated right eye aim invariant");
    bool rejected=false;try{make_pair(c,-1,2);}catch(...){rejected=true;}
    check(rejected,"invalid IPD rejected");
    std::puts("PASS camera geometry, right-eye anchor, convergence, vertical alignment, rotation, input guard");
}
