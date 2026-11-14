#pragma once
#include <cstdint>
#include <vector>
#include <cstring>
// September DX11 PDB layout only. Never allocate or free engine-owned containers.
namespace r1st {
struct VectorLayout {void* begin;void* end;void* capacity;};
struct DetailLists {
    struct Entry {VectorLayout* address;VectorLayout saved;};
    std::vector<Entry> entries;
    bool capture(void* details) {
        entries.clear();if(!details)return true;
        auto lists=reinterpret_cast<VectorLayout*>(static_cast<unsigned char*>(details)+1216);
        for(unsigned i=0;i<3;++i){
            auto b=reinterpret_cast<uintptr_t>(lists[i].begin),e=reinterpret_cast<uintptr_t>(lists[i].end);
            if(e<b||(e-b)%sizeof(VectorLayout)||(e-b)/sizeof(VectorLayout)>65536)return false;
            for(auto p=static_cast<VectorLayout*>(lists[i].begin);p!=lists[i].end;++p)entries.push_back({p,*p});
        }
        return true;
    }
    bool restore() {
        for(auto& e:entries)if(e.address->begin!=e.saved.begin||e.address->capacity!=e.saved.capacity)return false;
        for(auto& e:entries)e.address->end=e.saved.end;
        return true;
    }
};
struct LightFrames {
    struct Node {Node* left;Node* parent;Node* right;unsigned char color,nil;unsigned char pad[6];void* light;};
    struct Entry {unsigned* address;unsigned saved;};
    std::vector<Entry> entries;
    bool capture(void* tree){
        entries.clear();auto head=*static_cast<Node**>(tree);if(!head)return false;
        const auto count=*reinterpret_cast<size_t*>(static_cast<unsigned char*>(tree)+8);
        if(count>100000)return false;
        std::vector<Node*> pending;if(!head->parent->nil)pending.push_back(head->parent);
        while(!pending.empty()){
            auto n=pending.back();pending.pop_back();if(entries.size()>=count||!n->light)return false;
            auto stamp=reinterpret_cast<unsigned*>(static_cast<unsigned char*>(n->light)+192);
            entries.push_back({stamp,*stamp});
            if(!n->left->nil)pending.push_back(n->left);if(!n->right->nil)pending.push_back(n->right);
        }
        return entries.size()==count;
    }
    void restore(){for(auto& e:entries)*e.address=e.saved;}
};
}
