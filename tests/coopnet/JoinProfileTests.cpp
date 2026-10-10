#include "../../src/CoopNet/JoinProfile.h"
#include "../../src/CoopNet/ClientPump.h"
#include "../../src/CoopNet/HostPump.h"
#include <cstdlib>
#include <iostream>
using namespace coopnet;
void require(bool value) { if (!value) std::abort(); }
int main() {
    std::string address;
    require(normalize_endpoint("192.168.1.5",address) && address=="192.168.1.5:27888");
    require(normalize_endpoint("127.0.0.1:27889",address));
    for (const char* bad:{"", "256.1.1.1", "1.2.3", "1.2.3.4:0", "1.2.3.4:65536", "1.2.3.4;quit", "1.2.3.4:2 9", "224.0.0.1", "0.0.0.0"}) require(!normalize_endpoint(bad,address));
    HostPump host; ClientPump initial; Identity token=100;
    host.start(500,1,{10,20},[&] { return ++token; }); auto link=MemoryTransport::pair();
    require(host.attach(1,std::move(link.second))); initial.start(std::move(link.first),2,{10,20});
    for (unsigned i=0;i<5;++i) { initial.update(.01); host.update(.01); }
    JoinProfile profile{"127.0.0.1:27889",2,{10,20},initial.session().welcome()};
    const auto bytes=encode_join_profiles({profile}); std::vector<JoinProfile> restored;
    require(decode_join_profiles(bytes,restored) && restored.size()==1 && restored[0].resume.resume_token==profile.resume.resume_token);
    for (std::size_t n=0;n<bytes.size();++n) require(!decode_join_profiles({bytes.begin(),bytes.begin()+n},restored));
    auto trailing=bytes; trailing.push_back(0); require(!decode_join_profiles(trailing,restored));
    initial.stop(); host.update(.01);
    ClientPump reopened; auto new_link=MemoryTransport::pair(); require(host.attach(2,std::move(new_link.second)));
    reopened.start(std::move(new_link.first),profile.character,profile.build,&profile.resume);
    for (unsigned i=0;i<5;++i) { reopened.update(.01); host.update(.01); }
    require(reopened.session().state()==ClientState::Connected && reopened.session().welcome().player==profile.resume.player &&
        reopened.session().welcome().generation>profile.resume.generation);
    reopened.stop(); host.stop(); host.start(700,1,{10,20},[&] { return ++token; });
    ClientPump expired; auto expired_link=MemoryTransport::pair(); require(host.attach(3,std::move(expired_link.second)));
    expired.start(std::move(expired_link.first),profile.character,profile.build,&profile.resume);
    for (unsigned i=0;i<5;++i) { expired.update(.01); host.update(.01); }
    require(expired.session().state()==ClientState::Rejected && expired.session().welcome().result==Admission::InvalidResume);
    for (const char* faction : {"stalker","bandit","dolg","freedom","csky","ecolog","killer","army","monolith","zombied","greh","isg","renegade"}) {
        for (unsigned scenario=0;scenario<3;++scenario) {
            HostPump guarded; ClientPump guest; Identity next_token=1000;
            guarded.start(900,1,{10,20},[&] { return ++next_token; });
            guarded.require_character_profile(true);
            const std::string expected=std::string("actor_")+faction;
            guarded.set_character_handler([&](Identity,const InventoryView& character) { return character.community==expected; });
            auto connection=MemoryTransport::pair(); require(guarded.attach(1,std::move(connection.second)));
            guest.start(std::move(connection.first),2,{10,20});
            InventoryView character; character.actor=2; character.generation=character.level=character.revision=1;
            character.community=scenario==1 ? (expected=="actor_bandit" ? "actor_stalker" : "actor_bandit") : expected;
            character.money=12345; character.items={{1,1,"bandage",1,0xffff,0,0,0,0}};
            const auto original=encode_view_chunk({character,0,1});
            if (scenario!=2) guest.set_character_profile(character);
            for (unsigned i=0;i<8;++i) { guarded.update(.01); guest.update(.01); }
            require(encode_view_chunk({character,0,1})==original); // admission never rewrites the selected character
            if (!scenario) require(guest.session().state()==ClientState::Connected && guarded.player_ready(guest.session().welcome().player));
            else {
                require(guest.session().state()==ClientState::Rejected);
                const auto denied=guest.session().welcome();
                require(denied.result==(scenario==1 ? Admission::CharacterRejected : Admission::CharacterRequired));
                require(!denied.player && !denied.resume_token && !denied.session);
                guarded.update(1.1);
            }
        }
    }
    HostPump denied_host; Identity denied_token=8000;
    denied_host.start(901,1,{10,20},[&] { return ++denied_token; });
    denied_host.require_character_profile(true);
    denied_host.set_character_handler([](Identity,const InventoryView&) { return false; });
    auto denied_link=MemoryTransport::pair(); require(denied_host.attach(1,std::move(denied_link.second)));
    require(denied_link.first->send({Message::ClientHello,Channel::Control,Delivery::ReliableOrdered,1,
        encode_hello({protocol_version,{10,20},2,0,0,0})})==SendResult::Sent);
    denied_host.update(.01); Frame reply; require(denied_link.first->receive(reply));
    Welcome issued; require(decode_welcome(reply.payload,issued) && issued.resume_token);
    InventoryView wrong; wrong.actor=2; wrong.generation=wrong.level=wrong.revision=1; wrong.community="actor_bandit";
    require(denied_link.first->send({Message::CharacterProfile,Channel::Control,Delivery::ReliableOrdered,0,
        encode_view_chunk({wrong,0,0})})==SendResult::Sent);
    denied_host.update(.01); require(denied_link.first->receive(reply));
    Welcome denied; require(decode_welcome(reply.payload,denied) && denied.result==Admission::CharacterRejected);
    denied_host.update(1.1);
    auto retry_link=MemoryTransport::pair(); require(denied_host.attach(2,std::move(retry_link.second)));
    ClientPump retry; retry.start(std::move(retry_link.first),2,{10,20},&issued);
    for (unsigned i=0;i<8;++i) { denied_host.update(.01); retry.update(.01); }
    require(retry.session().state()==ClientState::Rejected && retry.session().welcome().result==Admission::InvalidResume);
    std::cout<<"Saved connections, rejected token invalidation and all-faction character admission tests passed\n";
}
