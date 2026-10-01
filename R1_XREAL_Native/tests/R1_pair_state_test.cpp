#include "../native/R1_pair_state.h"
#include <array>
#include <cassert>
#include <cstdio>
int main(){
    alignas(8) std::array<unsigned char,1288> details{};
    void* values[2]={reinterpret_cast<void*>(1),reinterpret_cast<void*>(2)};
    r1st::VectorLayout inner{values,values+2,values+2};
    auto outer=reinterpret_cast<r1st::VectorLayout*>(details.data()+1216);
    outer[0]={&inner,&inner+1,&inner+1};
    r1st::DetailLists d;assert(d.capture(details.data()));inner.end=inner.begin;
    assert(d.restore()&&inner.end==values+2);
    inner.capacity=nullptr;assert(!d.restore());
    using Node=r1st::LightFrames::Node;Node head{},node{};head.nil=1;head.parent=&node;
    alignas(8) unsigned char light[200]{};auto stamp=reinterpret_cast<unsigned*>(light+192);*stamp=8;
    node.left=node.right=&head;node.light=light;
    struct Tree {Node* head;size_t count;} tree{&head,1};
    r1st::LightFrames f;assert(f.capture(&tree));*stamp=9;f.restore();assert(*stamp==8);
    tree.count=0;assert(!f.capture(&tree));
    puts("PASS consumed detail-list restoration, allocation-change refusal, light frame restoration, invalid tree count");
}
