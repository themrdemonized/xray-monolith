#include "../native/R1_loading_text.h"
#include <cassert>
#include <cstdio>
int main(){
 const float box=800;
 const float words[]={180,230,250,190};
 float line=0,maxLine=0;
 for(float word:words){if(line+word>r1st::loadingWidth(box,true,3840,1080)){if(line>maxLine)maxLine=line;line=0;}line+=word;}
 if(line>maxLine)maxLine=line;
 assert(maxLine*2<=box); // Rasterized glyphs must remain inside the original card.
 assert(r1st::loadingWidth(box,false,3840,1080)==box);
 assert(r1st::loadingWidth(box,true,1920,1080)==box);
 assert(r1st::loadingWidth(box,true,3840,0)==box);
 puts("PASS loading wrap matches doubled glyphs; inactive/ordinary/unknown viewport unchanged");
}
