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
    std::cout<<"Saved connection bounds, address validation, new-process resume and expired-session rejection tests passed\n";
}
