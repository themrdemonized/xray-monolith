#pragma once
#include "Roster.h"
#include "Transport.h"
#include "ActorSnapshot.h"
#include "ActorPresence.h"
#include "LevelAssignment.h"
#include "ActorInput.h"
#include "Gameplay.h"
#include "InventoryView.h"
#include "WorldBaseline.h"
#include "WorldState.h"
#include "PartyTransition.h"
#include <functional>
#include <list>
namespace coopnet {
class HostPump {
    struct Peer {
        Connection id;
        std::unique_ptr<Transport> transport;
        std::deque<Frame> outgoing;
        std::size_t queued_bytes = 0;
        Identity player = 0;
        bool fresh = false, ready = false, rejected = false;
        std::uint32_t party_revision=0;
        double elapsed = 0;
        std::uint32_t level = 0;
        LevelAssignment assignment;
        bool assigned = false;
        double transfer_time = 0;
        ActorInput input;
        SequenceWindow inputs;
        double input_age = 1;
        struct Transaction { InventoryRequest request; InventoryResult result; };
        std::deque<Transaction> transactions;
        SequenceWindow transaction_sequences;
        double transaction_budget = 8;
        std::map<Identity,std::uint32_t> item_revisions;
        Identity item_cursor=0;
        WorldBaseline baseline;
        std::shared_ptr<const std::vector<std::uint8_t>> baseline_bytes;
        std::uint32_t baseline_offset=0;
        bool baseline_started=false, baseline_received=false;
        double baseline_time=0, baseline_budget=65536;
    };
    HostSession session_;
    PartyStatus party_status_;
    Identity id_ = 0;
    std::uint32_t revision_ = 0;
    std::function<Identity()> tokens_;
    std::list<Peer> peers_;
    std::map<Identity, ActorPresence> actors_;
    std::map<Identity, std::uint32_t> actor_generations_;
    std::deque<LevelFailure> failures_;
    std::map<Identity,ItemState> items_;
    std::function<InventoryResult(Identity,const InventoryRequest&)> inventory_handler_;
    void failed(Peer& peer, TransferFailure reason) {
        if (!peer.assigned) return;
        failures_.push_back({peer.player,peer.assignment,reason}); peer.assigned = false;
        if (reason != TransferFailure::Disconnected && !queue(peer, Frame{Message::LevelCancelled,
            Channel::Transition, Delivery::ReliableOrdered, peer.assignment.revision,
            encode_cancellation(peer.assignment, reason)})) peer.transport->close();
    }
    bool presence(Peer& peer, Message message, const ActorPresence& actor) {
        if (queue(peer, Frame{message, Channel::World, Delivery::ReliableOrdered, actor.generation,
            encode_presence(actor)})) return true;
        peer.transport->close(); return false;
    }
    bool queue(Peer& peer, Frame frame) {
        const auto size = frame.payload.size() + 16;
        if (peer.outgoing.size() >= 64 || size > 256 * 1024 - peer.queued_bytes) return false;
        peer.queued_bytes += size; peer.outgoing.push_back(std::move(frame)); return true;
    }
    void publish() {
        ++revision_;
        const auto payload = encode_roster(make_roster(session_, id_, revision_));
        for (auto& peer : peers_) if (peer.ready) {
            if (!queue(peer, Frame{Message::Roster, Channel::Control, Delivery::ReliableOrdered, revision_, payload}))
                peer.transport->close();
        }
    }
    bool flush(Peer& peer) {
        while (!peer.outgoing.empty()) {
            const auto result = peer.transport->send(peer.outgoing.front());
            if (result == SendResult::Backpressure) return true;
            if (result != SendResult::Sent) return false;
            peer.queued_bytes -= peer.outgoing.front().payload.size() + 16; peer.outgoing.pop_front();
        }
        return true;
    }
    bool receive(Peer& peer) {
        for (unsigned count = 0; count < 32; ++count) {
            Frame frame{};
            if (!peer.transport->receive(frame)) return true;
            if (!peer.player && !peer.rejected) {
                ClientHello hello;
                if (frame.message != Message::ClientHello || !decode_hello(frame.payload, hello)) return false;
                const auto welcome = session_.admit(peer.id, hello, tokens_());
                if (!queue(peer, Frame{Message::ServerHello, Channel::Control, Delivery::ReliableOrdered,
                    1, encode_welcome(welcome)})) return false;
                peer.rejected = welcome.result != Admission::Accepted;
                peer.player = welcome.player; peer.fresh = !hello.resume_session;
                peer.elapsed = 0;
            } else if (!peer.rejected && !peer.ready) {
                if (frame.message != Message::ClientReady || !frame.payload.empty()) return false;
                peer.ready = true; publish();
            } else if (frame.message == Message::Disconnect && frame.payload.empty()) return false;
            else if (peer.ready && frame.message == Message::LevelReady) {
                LevelAssignment ready;
                if (!decode_assignment(frame.payload, ready) || frame.sequence != ready.revision) return false;
                const auto distance = ready.revision - peer.assignment.revision;
                if (distance >= 0x80000000u) continue; // late acknowledgement of an older transfer
                if (ready.ticket != peer.assignment.ticket || ready.level != peer.assignment.level || distance) return false;
                if (!peer.assigned) continue; // exact duplicate or cancelled transfer cannot restore interest
                peer.assigned = false;
                if (!set_interest_level(peer.player, ready.level)) return false;
            }
            else if (peer.ready && frame.message == Message::ActorInput) {
                ActorInput input;
                if (!decode_input(frame.payload,input) || frame.sequence != input.sequence) return false;
                const auto actor = actors_.find(input.entity);
                if (actor == actors_.end()) continue; // a retired binding cannot drive anything
                if (actor->second.player != peer.player) return false;
                if (actor->second.generation != input.generation || actor->second.level != input.level ||
                    peer.level != input.level || peer.assigned) continue;
                if (peer.input.entity != input.entity || peer.input.generation != input.generation) peer.inputs = {};
                if (!peer.inputs.accept(input.sequence)) continue;
                peer.input = input; peer.input_age = 0;
            }
            else if (peer.ready && frame.message==Message::WorldReceived) {
                WorldBaseline received;
                if (!decode_baseline(frame.payload,received) || frame.sequence ||
                    !peer.baseline_started || peer.baseline_offset!=peer.baseline.size ||
                    !same_baseline(received,peer.baseline)) return false;
                peer.baseline_received=true; peer.baseline_bytes.reset();
            }
            else if (peer.ready && frame.message == Message::InventoryRequest) {
                InventoryRequest request;
                if (!decode_inventory_request(frame.payload,request) || frame.sequence != request.sequence) return false;
                InventoryResult result{request.item,0,request.sequence,0,InventoryStatus::Unavailable};
                bool replay = false;
                for (const auto& transaction : peer.transactions) if (transaction.request.sequence == request.sequence) {
                    if (encode_inventory_request(transaction.request) != frame.payload) return false;
                    result=transaction.result; replay=true; break;
                }
                if (!replay) {
                    if (!peer.transaction_sequences.accept(request.sequence)) result.status=InventoryStatus::Expired;
                    else {
                        const auto actor=actors_.find(request.actor);
                        if (actor!=actors_.end() && actor->second.player!=peer.player) return false;
                        if (actor==actors_.end() || actor->second.generation!=request.generation ||
                            actor->second.level!=request.level || peer.level!=request.level || peer.assigned)
                            result.status=InventoryStatus::Denied;
                        else if (peer.transaction_budget<1) result.status=InventoryStatus::Busy;
                        else {
                            peer.transaction_budget-=1;
                            if (inventory_handler_) result=inventory_handler_(peer.player,request);
                            // Adapter cannot change the transaction's request identity.
                            result.item=request.item; result.sequence=request.sequence;
                        }
                        peer.transactions.push_back({request,result});
                        if (peer.transactions.size()>256) peer.transactions.pop_front();
                    }
                }
                if (!queue(peer,{Message::InventoryResult,Channel::Inventory,Delivery::ReliableOrdered,
                    request.sequence,encode_inventory_result(result)})) return false;
            }
            else return false;
        }
        return true;
    }
public:
    bool level_ready(Identity player,std::uint32_t level) const {
        for (const auto& peer:peers_) if (peer.player==player)
            return peer.ready && !peer.assigned && peer.baseline_received && peer.level==level && peer.transport->connected();
        return false;
    }
    void suspend_world() {
        for (auto& peer:peers_) if (peer.ready) {
            if (peer.assigned) failed(peer,TransferFailure::Cancelled);
            set_interest_level(peer.player,0); peer.baseline={}; peer.baseline_bytes.reset();
            peer.baseline_started=false; peer.baseline_received=false;
        }
    }
    void publish_party_status(PartyStatus status) {
        if (!valid_party_status(status)) throw std::invalid_argument("Invalid party status");
        status.revision=party_status_.revision+1; party_status_=status;
    }
    Identity identity() const { return id_; }
    bool publish_world_state(const WorldState& state) {
        if (session_.mode()!=Mode::Host || !valid_world_state(state)) return false;
        const Frame frame{Message::WorldState,Channel::AI,Delivery::UnreliableSequenced,state.tick,encode_world_state(state)};
        for (auto& peer:peers_) if (peer.ready && peer.baseline_received && !peer.assigned && peer.level==state.level) {
            const auto result=peer.transport->send(frame);
            if (result!=SendResult::Sent && result!=SendResult::Backpressure) peer.transport->close();
        }
        return true;
    }
    bool participant_ready(Identity player) const {
        for (const auto& peer:peers_) if (peer.player==player) return peer.ready && peer.transport->connected();
        return false;
    }
    bool baseline_received(Identity player) const {
        for (const auto& peer:peers_) if (peer.player==player && peer.ready)
            return peer.baseline.id && peer.baseline_received;
        return false;
    }
    bool send_baseline(Identity player,const WorldBaseline& manifest,
        std::shared_ptr<const std::vector<std::uint8_t>> bytes) {
        if (!valid_baseline(manifest) || !bytes || bytes->size()!=manifest.size) return false;
        for (auto& peer:peers_) if (peer.player==player && peer.ready) {
            if (peer.assigned || (peer.baseline.id && !peer.baseline_received) || !set_interest_level(player,0)) return false;
            peer.baseline=manifest; peer.baseline_bytes=std::move(bytes); peer.baseline_offset=0;
            peer.baseline_started=false; peer.baseline_received=false; peer.baseline_time=0; peer.baseline_budget=65536;
            return true;
        }
        return false;
    }
    void set_inventory_handler(std::function<InventoryResult(Identity,const InventoryRequest&)> handler) {
        inventory_handler_=std::move(handler);
    }
    bool publish_inventory_view(Identity player,const InventoryView& view) {
        const auto actor=actors_.find(view.actor);
        if (actor==actors_.end() || actor->second.player!=player || actor->second.generation!=view.generation ||
            actor->second.level!=view.level || !valid_inventory_view(view)) return false;
        std::vector<Frame> frames; std::size_t bytes=0;
        for (std::size_t offset=0;offset<view.items.size() || frames.empty();offset+=32) {
            InventoryViewChunk chunk; chunk.view=view; chunk.view.items.clear();
            chunk.offset=static_cast<std::uint16_t>(offset); chunk.total=static_cast<std::uint16_t>(view.items.size());
            const auto end=(std::min)(view.items.size(),offset+32);
            chunk.view.items.assign(view.items.begin()+offset,view.items.begin()+end);
            auto payload=encode_view_chunk(chunk); bytes+=payload.size()+16;
            frames.push_back({Message::InventoryView,Channel::Inventory,Delivery::ReliableOrdered,view.revision,std::move(payload)});
        }
        for (auto& peer:peers_) if (peer.player==player && peer.ready && peer.level==view.level && peer.transport->connected()) {
            if (peer.outgoing.size()+frames.size()>48 || peer.queued_bytes>224*1024 || bytes>224*1024-peer.queued_bytes) return false;
            for (auto& frame:frames) if (!queue(peer,std::move(frame))) return false;
            return true;
        }
        return false;
    }
    bool publish_item(const ItemState& item) {
        if (session_.mode()!=Mode::Host || !valid_item_state(item)) return false;
        const auto found=items_.find(item.item);
        if (found==items_.end() && items_.size()>=4096) return false;
        if (found!=items_.end() && item.revision<=found->second.revision) return false;
        items_[item.item]=item; return true;
    }
    void clear_world_items() {
        for (auto it=items_.begin();it!=items_.end();) {
            if (it->second.world) it=items_.erase(it); else ++it;
        }
        for (auto& peer:peers_) {
          peer.item_revisions.clear();
          for (auto it=peer.outgoing.begin();it!=peer.outgoing.end();) {
            ItemState item;
            if (it->message==Message::ItemState && decode_item_state(it->payload,item) && item.world) {
                peer.queued_bytes-=it->payload.size()+16; it=peer.outgoing.erase(it);
            } else ++it;
          }
        }
    }
    bool publish_vitals(const ActorVitals& vitals) {
        const auto actor=actors_.find(vitals.actor);
        if (session_.mode()!=Mode::Host || !valid_vitals(vitals) || actor==actors_.end() ||
            actor->second.generation!=vitals.generation || actor->second.level!=vitals.level) return false;
        const Frame frame{Message::ActorVitals,Channel::Combat,Delivery::UnreliableSequenced,vitals.tick,encode_vitals(vitals)};
        for (auto& peer : peers_) if (peer.ready && peer.level==vitals.level) {
            const auto result=peer.transport->send(frame);
            if (result!=SendResult::Sent && result!=SendResult::Backpressure) peer.transport->close();
        }
        return true;
    }
    // Held controls expire after packet loss; callers apply them on host simulation ticks.
    bool latest_input(Identity player, ActorInput& output) const {
        for (const auto& peer : peers_) if (peer.player == player && peer.ready && !peer.assigned &&
            peer.input_age < .25 && peer.transport->connected()) {
            const auto actor = actors_.find(peer.input.entity);
            if (actor == actors_.end() || actor->second.player != player || actor->second.level != peer.level ||
                actor->second.generation != peer.input.generation) return false;
            output = peer.input; return true;
        }
        return false;
    }
    const HostSession& session() const { return session_; }
    unsigned ready_participants() const {
        unsigned count = session_.mode() == Mode::Host ? 1u : 0u;
        for (const auto& peer : peers_) if (peer.ready && peer.transport->connected()) ++count;
        return count;
    }
    // The host calls this only after its checkpoint and destination preparation.
    bool assign_level(Identity player, std::uint32_t level, Identity ticket) {
        if (!level || !ticket || failures_.size() >= 61) return false;
        for (auto& peer : peers_) if (peer.player == player && peer.ready) {
            if (peer.baseline.id && !peer.baseline_received) return false;
            if (peer.assigned || ticket == peer.assignment.ticket || !set_interest_level(player, 0)) return false;
            peer.assignment = {ticket,level,peer.assignment.revision + 1}; peer.assigned = true;
            peer.transfer_time = 0;
            if (!queue(peer, Frame{Message::LevelAssignment, Channel::Transition, Delivery::ReliableOrdered,
                peer.assignment.revision, encode_assignment(peer.assignment)})) { peer.transport->close(); return false; }
            return true;
        }
        return false;
    }
    bool cancel_level(Identity player) {
        for (auto& peer : peers_) if (peer.player == player && peer.assigned) {
            failed(peer, TransferFailure::Cancelled); return true;
        }
        return false;
    }
    bool take_level_failure(LevelFailure& output) {
        if (failures_.empty()) return false;
        output = failures_.front(); failures_.pop_front(); return true;
    }
    // Called by the host, never from a guest packet.
    bool set_interest_level(Identity player, std::uint32_t level) {
        for (auto& peer : peers_) if (peer.player == player && peer.ready) {
            if (peer.level == level) return true;
            for (const auto& actor : actors_) if (actor.second.level == peer.level)
                if (!presence(peer, Message::ActorRemove, actor.second)) return false;
            peer.level = level;
            peer.item_revisions.clear();
            peer.input_age = 1; // preserve sequence history across same-binding relevance changes
            for (const auto& actor : actors_) if (actor.second.level == level)
                if (!presence(peer, Message::ActorCreate, actor.second)) return false;
            return true;
        }
        return false;
    }
    bool publish_snapshot(const ActorSnapshot& snapshot) {
        if (session_.mode() != Mode::Host || !valid_snapshot(snapshot)) return false;
        const auto actor = actors_.find(snapshot.entity);
        if (actor == actors_.end() || actor->second.level != snapshot.level ||
            actor->second.generation != snapshot.generation) return false;
        const Frame frame{Message::ActorSnapshot, Channel::Actor, Delivery::UnreliableSequenced,
            snapshot.tick, encode_snapshot(snapshot)};
        for (auto& peer : peers_) if (peer.ready && peer.level == snapshot.level) {
            // Transient movement must not block durable control traffic or accumulate old poses.
            const auto result = peer.transport->send(frame);
            if (result != SendResult::Sent && result != SendResult::Backpressure) peer.transport->close();
        }
        return true;
    }
    bool create_actor(const ActorPresence& actor) {
        if (session_.mode() != Mode::Host || !valid_presence(actor) || actors_.count(actor.entity) || actors_.size() >= 4)
            return false;
        const auto previous = actor_generations_.find(actor.entity);
        if (previous != actor_generations_.end() && actor.generation <= previous->second) return false;
        if (previous == actor_generations_.end() && actor_generations_.size() >= 4096) return false;
        bool owner = false;
        for (const auto& player : session_.players())
            if (player.id == actor.player && player.character == actor.character && player.connected) owner = true;
        if (!owner) return false;
        for (const auto& entry : actors_) if (entry.second.player == actor.player) return false;
        actors_.emplace(actor.entity, actor);
        actor_generations_[actor.entity] = actor.generation;
        for (auto& peer : peers_) if (peer.ready && peer.level == actor.level) presence(peer, Message::ActorCreate, actor);
        return true;
    }
    bool remove_actor(Identity entity, std::uint32_t generation) {
        auto found = actors_.find(entity);
        if (found == actors_.end() || found->second.generation != generation) return false;
        for (auto& peer : peers_) if (peer.ready && peer.level == found->second.level)
            presence(peer, Message::ActorRemove, found->second);
        actors_.erase(found); return true;
    }
    void start(Identity id, Identity character, BuildIdentity build, std::function<Identity()> tokens) {
        if (!tokens) throw std::invalid_argument("Missing reconnect token source");
        session_.start(id, character, build); id_ = id; revision_ = 0; tokens_ = std::move(tokens);
    }
    bool attach(Connection id, std::unique_ptr<Transport> transport) {
        if (!transport || !id || session_.mode() != Mode::Host || peers_.size() >= 3) return false;
        for (const auto& peer : peers_) if (peer.id == id) return false;
        Peer peer{}; peer.id = id; peer.transport = std::move(transport);
        peers_.push_back(std::move(peer)); return true;
    }
    void update(double elapsed) {
        if (!std::isfinite(elapsed) || elapsed < 0) throw std::invalid_argument("Invalid pump time");
        for (auto it = peers_.begin(); it != peers_.end(); ) {
            auto& peer = *it;
            peer.elapsed += elapsed;
            peer.input_age += elapsed;
            peer.transaction_budget=(std::min)(8.,peer.transaction_budget+elapsed*8.);
            if (peer.baseline.id && !peer.baseline_received) {
                peer.baseline_time+=elapsed;
                peer.baseline_budget=(std::min)(65536.,peer.baseline_budget+elapsed*1024*1024);
            }
            if (peer.assigned) {
                peer.transfer_time += elapsed;
                if (peer.transfer_time >= 120) failed(peer, TransferFailure::Timeout);
            }
            bool keep = peer.transport->connected() || peer.transport->connecting();
            if (peer.baseline.id && !peer.baseline_received && peer.baseline_time>=120) keep=false;
            if ((!peer.ready && peer.elapsed >= 10) || (peer.rejected && peer.elapsed >= 1)) keep = false;
            if (keep && peer.transport->connected()) {
                keep=receive(peer);
                if (keep && peer.ready && peer.party_revision!=party_status_.revision && peer.outgoing.size()<48) {
                    keep=queue(peer,{Message::PartyStatus,Channel::Transition,Delivery::ReliableOrdered,
                        party_status_.revision,encode_party_status(party_status_)});
                    if (keep) peer.party_revision=party_status_.revision;
                }
                if (keep && peer.baseline_bytes && !peer.baseline_started && peer.outgoing.size()<48) {
                    keep=queue(peer,{Message::WorldBaseline,Channel::World,Delivery::ReliableOrdered,0,encode_baseline(peer.baseline)});
                    peer.baseline_started=keep;
                }
                for (unsigned n=0;keep && peer.baseline_bytes && peer.baseline_started &&
                    peer.baseline_offset<peer.baseline.size && peer.outgoing.size()<48 && n<8;++n) {
                    const auto size=(std::min)(baseline_chunk_bytes,peer.baseline.size-peer.baseline_offset);
                    // Reserve room for control/results while a socket is applying backpressure.
                    if (peer.baseline_budget<size || peer.queued_bytes+size+30>224*1024) break;
                    const auto begin=peer.baseline_bytes->begin()+peer.baseline_offset;
                    WorldChunk chunk{peer.baseline.id,peer.baseline_offset,{begin,begin+size}};
                    keep=queue(peer,{Message::WorldChunk,Channel::World,Delivery::ReliableOrdered,
                        peer.baseline_offset/baseline_chunk_bytes,encode_world_chunk(chunk)});
                    if (keep) { peer.baseline_offset+=size; peer.baseline_budget-=size; }
                }
                unsigned published=0;
                auto item_it=items_.upper_bound(peer.item_cursor);
                for (std::size_t visited=0;visited<items_.size() && keep && peer.ready && peer.outgoing.size()<48 && published<8;++visited) {
                    if (item_it==items_.end()) item_it=items_.begin();
                    const auto& item=*item_it++; peer.item_cursor=item.first;
                    if (item.second.level!=peer.level || peer.item_revisions[item.first]==item.second.revision) continue;
                    keep=queue(peer,{Message::ItemState,Channel::Inventory,Delivery::ReliableOrdered,
                        item.second.revision,encode_item_state(item.second)});
                    if (keep) { peer.item_revisions[item.first]=item.second.revision; ++published; }
                }
                if (keep) keep=flush(peer);
            }
            if (keep) { ++it; continue; }
            peer.transport->close();
            failed(peer, TransferFailure::Disconnected);
            const bool visible = peer.ready;
            session_.disconnect(peer.id);
            for (auto actor = actors_.begin(); actor != actors_.end(); ) {
                const auto value = actor->second; ++actor;
                if (value.player == peer.player) remove_actor(value.entity, value.generation);
            }
            if (peer.fresh && !peer.ready && peer.player) session_.release(peer.player);
            it = peers_.erase(it);
            if (visible) publish();
        }
        // A later peer's disconnect may enqueue a roster for an already visited peer.
        for (auto& peer : peers_)
            if (peer.transport->connected() && !flush(peer)) peer.transport->close();
    }
    void stop() {
        for (auto& peer : peers_) peer.transport->close();
        peers_.clear(); actors_.clear(); actor_generations_.clear(); failures_.clear(); session_.stop(); tokens_ = {}; id_ = 0;
        items_.clear(); inventory_handler_={};
    }
};
}
