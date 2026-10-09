#pragma once
#include "Gameplay.h"
#include <map>
#include <deque>
namespace coopnet {
// A settling/moving loose item can change revision between a click and receipt.
// Retry only explicit conflicts, with fresh sequences and unchanged ownership/incarnation.
class LootRetries {
    struct Entry { InventoryRequest request; Identity incarnation=0; unsigned attempts=0; double due=0,expiry=0; };
    std::map<std::uint32_t,Entry> pending_;
    std::deque<Entry> retries_;
    Entry popped_;
    double time_=0;
public:
    void clear() { pending_.clear(); retries_.clear(); popped_={}; time_=0; }
    void defer() { if (popped_.request.item && time_<=popped_.expiry && retries_.size()<32) { popped_.due=time_+.15; retries_.push_back(popped_); } popped_={}; }
    void advance(double elapsed) {
        if (!std::isfinite(elapsed) || elapsed<0) return; time_+=elapsed;
        for (auto it=pending_.begin();it!=pending_.end();) if (it->second.expiry<time_) it=pending_.erase(it); else ++it;
    }
    bool sent(const InventoryRequest& request,const ItemState& item,unsigned attempts=0) {
        if (!valid_inventory_request(request) || !item.world || !item.present || !item.incarnation || request.item!=item.item || request.level!=item.level || attempts>3 || pending_.size()>=32 ||
            !((request.action==InventoryAction::Take && !item.owner) || (request.action==InventoryAction::Drop && item.owner==request.actor))) return false;
        pending_[request.sequence]={request,item.incarnation,attempts,0,time_+8}; return true;
    }
    bool completed(const InventoryResult& result) {
        const auto found=pending_.find(result.sequence); if (found==pending_.end() || found->second.request.item!=result.item) return false;
        auto entry=found->second; pending_.erase(found);
        if (result.status!=InventoryStatus::Conflict || !result.revision || entry.attempts>=3 || retries_.size()>=32) return false;
        entry.request.revision=result.revision; ++entry.attempts; entry.due=time_+.15; entry.expiry=time_+2; retries_.push_back(entry); return true;
    }
    bool pop(const std::map<Identity,ItemState>& items,InventoryRequest& request,unsigned& attempts) {
        for (auto it=retries_.begin();it!=retries_.end();) {
            const auto item=items.find(it->request.item);
            if (time_>it->expiry || item==items.end() || !item->second.present || !item->second.world || item->second.level!=it->request.level || item->second.incarnation!=it->incarnation ||
                (it->request.action==InventoryAction::Take ? item->second.owner!=0 : item->second.owner!=it->request.actor)) { it=retries_.erase(it); continue; }
            if (time_<it->due || item->second.revision<it->request.revision) { ++it; continue; }
            popped_=*it; request=it->request; request.revision=item->second.revision; attempts=it->attempts; retries_.erase(it); return true;
        } return false;
    }
};
}
