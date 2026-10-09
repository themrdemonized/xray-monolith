#include "../../src/CoopNet/LootRetries.h"
#include <iostream>
using namespace coopnet;
void check(bool value,int line) { if (!value) { std::cerr<<"Loot retry failed at "<<line<<'\n'; std::exit(1); } }
#define require(v) check((v),__LINE__)
int main() {
    ItemState item; item.item=50; item.world=true; item.present=true; item.level=10; item.revision=3; item.incarnation=7;
    InventoryRequest request{100,50,1,10,1,3,InventoryAction::Take},retry; unsigned attempts=0; std::map<Identity,ItemState> items{{50,item}}; LootRetries queue;
    require(queue.sent(request,item)); require(queue.completed({50,0,1,4,InventoryStatus::Conflict}));
    require(!queue.pop(items,retry,attempts)); queue.advance(.2); require(!queue.pop(items,retry,attempts)); items[50].revision=4;
    require(queue.pop(items,retry,attempts) && attempts==1 && retry.revision==4);
    for (unsigned n=1;n<=3;++n) {
        retry.sequence=n+1; require(queue.sent(retry,items[50],n));
        const bool scheduled=queue.completed({50,0,retry.sequence,4,InventoryStatus::Conflict}); require(scheduled==(n<3)); queue.advance(.2);
        if (n<3) require(queue.pop(items,retry,attempts) && attempts==n+1); else require(!queue.pop(items,retry,attempts));
    }
    queue.clear(); require(queue.sent(request,item)); require(!queue.completed({50,0,1,3,InventoryStatus::OutOfRange})); queue.advance(.2); require(!queue.pop(items,retry,attempts));
    require(queue.sent(request,item)); require(queue.completed({50,0,1,4,InventoryStatus::Conflict})); items[50].owner=101; queue.advance(.2); require(!queue.pop(items,retry,attempts));
    items[50].owner=0; require(queue.sent(request,item)); require(queue.completed({50,0,1,4,InventoryStatus::Conflict})); items[50].incarnation=8; queue.advance(.2); require(!queue.pop(items,retry,attempts));
    items[50]=item; request.action=InventoryAction::Drop; item.owner=100; items[50]=item; require(queue.sent(request,item)); require(queue.completed({50,100,1,4,InventoryStatus::Conflict})); items[50].revision=4; queue.advance(.2); require(queue.pop(items,retry,attempts) && retry.action==InventoryAction::Drop);
    require(queue.sent(request,item)); require(queue.completed({50,100,1,4,InventoryStatus::Conflict})); queue.advance(3); require(!queue.pop(items,retry,attempts));
    queue.clear(); require(queue.sent(request,item)); require(queue.completed({50,100,1,4,InventoryStatus::Conflict})); queue.advance(.2); require(queue.pop(items,retry,attempts)); queue.defer();
    require(!queue.pop(items,retry,attempts)); queue.advance(.2); require(queue.pop(items,retry,attempts) && attempts==1); queue.defer(); queue.advance(2); require(!queue.pop(items,retry,attempts));
    std::cout<<"Loot revision retry ownership/incarnation guards, denial isolation, latest-state wait, attempt bounds and expiry passed\n";
}
