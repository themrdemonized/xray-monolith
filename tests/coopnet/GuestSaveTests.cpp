#include "../../src/CoopNet/GuestSave.h"
#include <iostream>
using namespace engine_coopnet;
void check(bool value,int line) { if (!value) { std::cerr<<"Guest save failure "<<line<<'\n'; std::exit(1); } }
#define require(v) check((v),__LINE__)
int main() {
    GuestSave save; save.scope=10; save.character=20; save.game=30; save.mods=40; save.sequence=5;
    save.condition={.75f,.8f,.1f}; save.inventory.active_slot=2;
    save.inventory.money=314159; save.inventory.has_money=true;
    save.inventory.community="actor_stalker";
    save.inventory.items={{"wpn_pm",{1,2,3}},{"ammo_9x18_fmj",{4,5,6}}};
    const auto bytes=encode_guest_save(save); GuestSave decoded;
    require(decode_guest_save(bytes,decoded));
    require(decoded.scope==10 && decoded.character==20 && decoded.sequence==5 && decoded.condition.health==.75f);
    require(decoded.inventory.active_slot==2 && decoded.inventory.items.size()==2 && decoded.inventory.items[1].spawn==save.inventory.items[1].spawn);
    require(decoded.inventory.has_money && decoded.inventory.money==314159);
    require(decoded.inventory.community=="actor_stalker");
    // A pre-money GCS1 record must preserve its native items and avoid resetting
    // a character's starting money when upgraded.
    auto legacy2=bytes; legacy2[3]='2'; legacy2.erase(legacy2.begin()+65,legacy2.begin()+66+save.inventory.community.size());
    require(decode_guest_save(legacy2,decoded) && decoded.inventory.community.empty() && decoded.inventory.money==314159);
    auto legacy=legacy2; legacy[3]='1'; legacy.erase(legacy.begin()+60,legacy.begin()+65);
    require(decode_guest_save(legacy,decoded) && !decoded.inventory.has_money && decoded.inventory.money==0 && decoded.inventory.items.size()==2);
    auto bad_money=bytes; bad_money[60]=2; require(!decode_guest_save(bad_money,decoded));
    save.inventory.money=std::numeric_limits<std::uint32_t>::max();
    require(decode_guest_save(encode_guest_save(save),decoded) && decoded.inventory.money==std::numeric_limits<std::uint32_t>::max());
    for (std::size_t n=0;n<bytes.size();++n) { decoded.character=99; require(!decode_guest_save({bytes.begin(),bytes.begin()+n},decoded)); require(decoded.character==99); }
    auto extra=bytes; extra.push_back(0); require(!decode_guest_save(extra,decoded));
    auto bad=save; bad.condition.health=std::numeric_limits<float>::quiet_NaN(); require(!valid_guest_save(bad));
    bad=save; bad.inventory.items[0].section="../wpn_pm"; require(!valid_guest_save(bad));
    bad=save; bad.inventory.items.resize(257); require(!valid_guest_save(bad));
    bad=save; bad.inventory.items[0].spawn.resize(16384); require(!valid_guest_save(bad));
    bad=save; bad.sequence=0; require(!valid_guest_save(bad));
    save.inventory.items.clear(); require(decode_guest_save(encode_guest_save(save),decoded) && decoded.inventory.items.empty());
    std::cout<<"Guest save tests passed\n";
}
