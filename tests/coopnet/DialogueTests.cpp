#include "../../src/CoopNet/Dialogue.h"
#include "../../src/CoopNet/HostPump.h"
#include "../../src/CoopNet/ClientPump.h"
#include <iostream>
using namespace coopnet;
void check(bool value,int line) { if (!value) { std::cerr<<"Dialogue failed at "<<line<<'\n'; std::exit(1); } }
#define require(v) check((v),__LINE__)
int main() {
    DialogueRequest request{10,20,2,3,4,5,0,DialogueAction::Open,{},{}},decoded;
    auto bytes=encode_dialogue_request(request); require(decode_dialogue_request(bytes,decoded));
    for (std::size_t i=0;i<bytes.size();++i) require(!decode_dialogue_request({bytes.begin(),bytes.begin()+i},decoded));
    auto extra=bytes; extra.push_back(0); require(!decode_dialogue_request(extra,decoded));
    DialogueView view{10,20,2,3,4,6,false,{{"quest_offer","","Accept a quest"},{"quest_turnin","2","Turn in"}}},out;
    view.answers={{true,"Accept this job"},{false,"Here are the details"}};
    bytes=encode_dialogue_view(view); require(decode_dialogue_view(bytes,out));
    require(out.answers.size()==2 && out.answers[0].player && !out.answers[1].player && out.answers[1].text=="Here are the details");
    auto invalid_speaker=bytes; invalid_speaker[bytes.size()-1-3-view.answers[0].text.size()-3-view.answers[1].text.size()+1]=2;
    require(!decode_dialogue_view(invalid_speaker,out));
    auto invalid_answer=view; invalid_answer.answers.resize(65,{false,"x"}); require(!valid_dialogue_view(invalid_answer));
    invalid_answer=view; invalid_answer.answers[0].text=std::string(4097,'x'); require(!valid_dialogue_view(invalid_answer));
    invalid_answer=view; invalid_answer.answers[0].text=std::string("a\0b",3); require(!valid_dialogue_view(invalid_answer));
    for (std::size_t i=0;i<bytes.size();++i) require(!decode_dialogue_view({bytes.begin(),bytes.begin()+i},out));
    extra=bytes; extra.push_back(0); require(!decode_dialogue_view(extra,out));
    view.answers.clear();
    request.action=DialogueAction::Select; request.revision=6; request.dialog="quest_offer";
    require(offered_dialogue_choice(view,request));
    request.dialog="quest_turnin"; request.phrase="2"; require(offered_dialogue_choice(view,request));
    for (unsigned change=0;change<8;++change) {
        auto invalid=request;
        if (change==0) ++invalid.actor; if (change==1) ++invalid.target; if (change==2) ++invalid.incarnation;
        if (change==3) ++invalid.generation; if (change==4) ++invalid.level; if (change==5) --invalid.revision;
        if (change==6) invalid.dialog="unoffered_reward"; if (change==7) invalid.phrase="99";
        require(!offered_dialogue_choice(view,invalid));
    }
    view.finished=true; view.choices.clear(); require(valid_dialogue_view(view) && !offered_dialogue_choice(view,request));
    view.finished=false; view.choices={{"quest_offer","0","Accept"},{"quest_offer","0","Duplicate"}}; require(!valid_dialogue_view(view));
    view.choices={{"quest_offer","0",std::string(4097,'x')}}; require(!valid_dialogue_view(view));
    view.choices.clear(); for (unsigned i=0;i<256;++i) view.choices.push_back({"quest_offer",std::to_string(i),std::string(4096,'x')}); require(!valid_dialogue_view(view));
    request.dialog="../reward"; require(!valid_dialogue_request(request));
    view.choices={{"quest_offer","0",std::string(4096,'a')},{"quest_offer","1",std::string(4096,'b')}};
    bytes=encode_dialogue_view(view); require(bytes.size()>8192);
    DialogueChunk first{10,3,4,6,5,static_cast<std::uint32_t>(bytes.size()),0,{bytes.begin(),bytes.begin()+8192}},chunk;
    auto wire=encode_dialogue_chunk(first); require(decode_dialogue_chunk(wire,chunk));
    for (std::size_t i=0;i<wire.size();++i) require(!decode_dialogue_chunk({wire.begin(),wire.begin()+i},chunk));
    DialogueAssembly assembly; require(assembly.accept(first) && !assembly.complete() && !assembly.view(out));
    DialogueChunk last=first; last.offset=8192; last.bytes.assign(bytes.begin()+8192,bytes.end());
    for (unsigned change=0;change<6;++change) {
        auto invalid=last;
        if (change==0) ++invalid.actor; if (change==1) ++invalid.generation; if (change==2) ++invalid.level;
        if (change==3) ++invalid.revision; if (change==4) ++invalid.sequence; if (change==5) --invalid.total;
        require(!assembly.accept(invalid));
    }
    require(assembly.accept(last) && assembly.complete() && assembly.view(out) && out.choices.size()==2);
    require(!assembly.accept(last)); assembly.clear(); require(!assembly.view(out));
    first.actor=99; require(assembly.accept(first)); last.actor=99; require(assembly.accept(last) && !assembly.view(out));
    HostPump host; ClientPump client; auto links=MemoryTransport::pair(); Identity token=100;
    host.start(1,1,{1,1},[&] { return ++token; }); require(host.attach(1,std::move(links.second))); client.start(std::move(links.first),2,{1,1});
    auto pump=[&] { for (unsigned i=0;i<8;++i) { host.update(.01); client.update(.01); } };
    pump(); const auto player=client.session().welcome().player;
    require(host.create_actor({10,player,2,3,4}));
    auto baseline_bytes=std::make_shared<std::vector<std::uint8_t>>(std::size_t(12),std::uint8_t(0)); WorldBaseline baseline{700,4,12,{}};
    client.set_baseline_sink([](const WorldBaseline&,const std::vector<std::uint8_t>&) { return true; });
    require(host.send_baseline(player,baseline,baseline_bytes)); pump(); require(client.acknowledge_baseline()); pump();
    require(host.assign_level(player,4,800)); pump(); require(client.acknowledge_level(4)); pump();
    unsigned calls=0,mutations=0,replies=0; DialogueView latest;
    host.set_dialogue_handler([&](Identity owner,const DialogueRequest& r,std::uint32_t revision) {
        require(owner==player); ++calls;
        DialogueView response{r.actor,r.target,r.incarnation,r.generation,r.level,revision,false,{{"quest_offer","0",std::string(4096,'a')},{"quest_offer","1",std::string(4096,'b')}}};
        response.answers={{false,"Host NPC reply"}};
        if (r.action==DialogueAction::Select) { require(offered_dialogue_choice(latest,r)); ++mutations; response.finished=true; response.choices.clear(); }
        return response;
    });
    client.set_dialogue_sink([&](const DialogueView& response) { latest=response; ++replies; });
    request={10,20,2,3,4,1,0,DialogueAction::Open,{},{}};
    require(client.send_dialogue(request)==SendResult::Sent);
    auto overlapping=request; overlapping.sequence=2; require(client.send_dialogue(overlapping)==SendResult::Backpressure);
    pump(); require(calls==1 && replies==1 && latest.choices.size()==2);
    require(latest.answers.size()==1 && latest.answers[0].text=="Host NPC reply" && !latest.answers[0].player);
    require(client.send_dialogue(request)==SendResult::Sent); pump(); require(calls==1 && replies==2);
    auto changed=request; changed.target=21; require(client.send_dialogue(changed)==SendResult::Invalid);
    request.sequence=2; request.revision=latest.revision; request.action=DialogueAction::Select; request.dialog="quest_offer"; request.phrase="0";
    require(client.send_dialogue(request)==SendResult::Sent); pump(); require(calls==2 && mutations==1 && latest.finished);
    require(client.send_dialogue(request)==SendResult::Sent); pump(); require(calls==2 && mutations==1);
    require(host.assign_level(player,4,801)); pump(); require(client.acknowledge_level(4)); pump();
    require(client.send_dialogue(request)==SendResult::Invalid);
    request={10,20,2,3,4,3,0,DialogueAction::Open,{},{}};
    require(client.send_dialogue(request)==SendResult::Sent);
    // Retire the pending interaction before the host consumes it. An old reply must
    // not block a fresh interaction after the destination becomes ready.
    const auto before_travel=replies;
    require(host.assign_level(player,4,802)); pump();
    require(replies==before_travel);
    require(client.acknowledge_level(4)); pump();
    request.sequence=4; require(client.send_dialogue(request)==SendResult::Sent);
    pump(); require(calls==3 && replies==before_travel+1);
    auto foreign=request; foreign.actor=99; foreign.sequence=5;
    require(client.send_dialogue(foreign)==SendResult::Invalid);
    request.sequence=5; require(client.send_dialogue(request)==SendResult::Sent);
    require(host.remove_actor(10,3)); require(host.create_actor({10,player,2,4,4}));
    pump(); require(replies==before_travel+1);
    request.sequence=6; request.generation=4;
    require(client.send_dialogue(request)==SendResult::Sent); pump();
    require(calls==4 && replies==before_travel+2);
    require(host.assign_level(player,4,803)); pump();
    require(host.cancel_level(player)); pump();
    require(client.transfer_failure()==TransferFailure::Cancelled);
    require(client.send_dialogue(request)==SendResult::Disconnected);
    std::cout<<"Dialogue codecs, bounds, offered choices and actor/NPC/incarnation/level/revision isolation passed\n";
}
