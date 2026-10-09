#pragma once
#include "Gameplay.h"
#include <set>
#include <algorithm>
namespace coopnet {
// Explicit item data, never native spawn packets or native object IDs.
struct InventoryViewItem {
    Identity item=0;
    std::uint32_t revision=0;
    std::string section;
    float condition=1;
    std::uint16_t slot=0xffff,ammo=0;
    std::uint8_t place=0,kind=0,ammo_type=0;
    std::uint8_t addons=0,scope=0,uses=0;
    std::vector<std::string> upgrades;
};
struct InventoryView {
    Identity actor=0,active=0;
    std::uint32_t generation=0,level=0,revision=0;
    std::vector<InventoryViewItem> items;
    std::uint32_t money=0;
};
struct InventoryViewChunk {
    InventoryView view;
    std::uint16_t offset=0,total=0;
};
inline bool valid_view_item(const InventoryViewItem& item) {
    return valid_item_state({item.item,0,1,item.revision,true,item.section}) &&
        std::isfinite(item.condition) && item.condition>=0 && item.condition<=1 && item.place<=2 && item.kind<=2 &&
        (item.place!=2 || item.slot<256) && item.addons<=7 && item.upgrades.size()<=16 &&
        std::all_of(item.upgrades.begin(),item.upgrades.end(),[](const std::string& upgrade) {
            return valid_item_state({1,0,1,1,true,upgrade});
        });
}
inline bool valid_view_chunk(const InventoryViewChunk& chunk) {
    const auto& v=chunk.view;
    if (!v.actor || !v.generation || !v.level || !v.revision || chunk.total>256 || v.items.size()>32 ||
        chunk.offset+v.items.size()>chunk.total || (chunk.total && v.items.empty()) || (!chunk.total && chunk.offset)) return false;
    std::set<Identity> identities;
    for (const auto& item:v.items) if (!valid_view_item(item) || !identities.insert(item.item).second) return false;
    return true;
}
inline bool valid_inventory_view(const InventoryView& view) {
    if (!view.actor || !view.generation || !view.level || !view.revision || view.items.size()>256) return false;
    std::set<Identity> identities; std::set<std::uint16_t> slots; bool active=!view.active;
    for (const auto& item:view.items) {
        if (!valid_view_item(item) || !identities.insert(item.item).second ||
            (item.place==2 && !slots.insert(item.slot).second)) return false;
        if (item.item==view.active) { if (item.place!=2) return false; active=true; }
    }
    return active;
}
inline std::vector<std::uint8_t> encode_view_chunk(const InventoryViewChunk& chunk) {
    if (!valid_view_chunk(chunk)) throw std::invalid_argument("Invalid inventory view chunk");
    const auto& v=chunk.view; Writer w;
    w.integer(v.actor,8); w.integer(v.active,8); w.integer(v.generation,4); w.integer(v.level,4); w.integer(v.revision,4);
    w.integer(chunk.offset,2); w.integer(chunk.total,2); w.integer(v.items.size(),1);
    w.integer(v.money,4);
    for (const auto& item:v.items) {
        w.integer(item.item,8); w.integer(item.revision,4); write_float(w,item.condition);
        w.integer(item.slot,2); w.integer(item.ammo,2); w.integer(item.place,1); w.integer(item.kind,1); w.integer(item.ammo_type,1);
        w.integer(item.section.size(),1); w.bytes.insert(w.bytes.end(),item.section.begin(),item.section.end());
        w.integer(item.addons,1); w.integer(item.scope,1); w.integer(item.uses,1); w.integer(item.upgrades.size(),1);
        for (const auto& upgrade:item.upgrades) { w.integer(upgrade.size(),1); w.bytes.insert(w.bytes.end(),upgrade.begin(),upgrade.end()); }
    }
    return w.bytes;
}
inline bool decode_view_chunk(const std::vector<std::uint8_t>& bytes,InventoryViewChunk& output) {
    Reader r(bytes); InventoryViewChunk c; std::uint64_t g,l,rev,offset,total,count;
    if (!r.integer(c.view.actor,8) || !r.integer(c.view.active,8) || !r.integer(g,4) || !r.integer(l,4) || !r.integer(rev,4) ||
        !r.integer(offset,2) || !r.integer(total,2) || !r.integer(count,1) || count>32 || total>256) return false;
    c.view.generation=static_cast<std::uint32_t>(g); c.view.level=static_cast<std::uint32_t>(l); c.view.revision=static_cast<std::uint32_t>(rev);
    c.offset=static_cast<std::uint16_t>(offset); c.total=static_cast<std::uint16_t>(total);
    std::uint64_t money; if (!r.integer(money,4)) return false; c.view.money=static_cast<std::uint32_t>(money);
    for (std::uint64_t n=0;n<count;++n) {
        InventoryViewItem item; std::uint64_t revision,slot,ammo,place,kind,type,size;
        if (!r.integer(item.item,8) || !r.integer(revision,4) || !read_float(r,item.condition) || !r.integer(slot,2) ||
            !r.integer(ammo,2) || !r.integer(place,1) || !r.integer(kind,1) || !r.integer(type,1) || !r.integer(size,1) || size>128 || size>r.remaining()) return false;
        item.revision=static_cast<std::uint32_t>(revision); item.slot=static_cast<std::uint16_t>(slot); item.ammo=static_cast<std::uint16_t>(ammo);
        item.place=static_cast<std::uint8_t>(place); item.kind=static_cast<std::uint8_t>(kind); item.ammo_type=static_cast<std::uint8_t>(type);
        for (std::uint64_t s=0;s<size;++s) { std::uint64_t ch; if (!r.integer(ch,1)) return false; item.section.push_back(static_cast<char>(ch)); }
        std::uint64_t addons,scope,uses,upgrades;
        if (!r.integer(addons,1) || !r.integer(scope,1) || !r.integer(uses,1) || !r.integer(upgrades,1) || upgrades>16) return false;
        item.addons=static_cast<std::uint8_t>(addons); item.scope=static_cast<std::uint8_t>(scope); item.uses=static_cast<std::uint8_t>(uses);
        for (std::uint64_t u=0;u<upgrades;++u) {
            if (!r.integer(size,1) || !size || size>128 || size>r.remaining()) return false;
            std::string upgrade;
            for (std::uint64_t s=0;s<size;++s) { std::uint64_t ch; if (!r.integer(ch,1)) return false; upgrade.push_back(static_cast<char>(ch)); }
            item.upgrades.push_back(std::move(upgrade));
        }
        c.view.items.push_back(std::move(item));
    }
    if (r.remaining() || !valid_view_chunk(c)) return false;
    output=std::move(c); return true;
}
class InventoryViewAssembly {
    InventoryView pending_;
    std::uint16_t total_=0;
    std::set<Identity> identities_;
public:
    void clear() { pending_={}; total_=0; identities_.clear(); }
    bool append(const InventoryViewChunk& chunk,bool& complete,InventoryView& output) {
        complete=false;
        if (!valid_view_chunk(chunk)) return false;
        if (!chunk.offset) { clear(); pending_=chunk.view; pending_.items.clear(); total_=chunk.total; }
        if (!pending_.actor || pending_.actor!=chunk.view.actor || pending_.active!=chunk.view.active ||
            pending_.generation!=chunk.view.generation || pending_.level!=chunk.view.level || pending_.revision!=chunk.view.revision ||
            pending_.money!=chunk.view.money || total_!=chunk.total || pending_.items.size()!=chunk.offset) return false;
        for (const auto& item:chunk.view.items) if (!identities_.insert(item.item).second) return false;
        pending_.items.insert(pending_.items.end(),chunk.view.items.begin(),chunk.view.items.end());
        if (pending_.items.size()==total_) {
            if (!valid_inventory_view(pending_)) return false;
            output=std::move(pending_); complete=true; clear();
        }
        return true;
    }
};
}
