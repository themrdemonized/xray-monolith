#pragma once
#include "Roster.h"
#include "Transport.h"
#include "ActorSnapshot.h"
#include "ActorPresence.h"
#include "LevelAssignment.h"
#include "ActorInput.h"
#include "Gameplay.h"
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
    };
    HostSession session_;
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
    void set_inventory_handler(std::function<InventoryResult(Identity,const InventoryRequest&)> handler) {
        inventory_handler_=std::move(handler);
    }
    bool publish_item(const ItemState& item) {
        if (session_.mode()!=Mode::Host || !valid_item_state(item)) return false;
        const auto found=items_.find(item.item);
        if (found==items_.end() && items_.size()>=4096) return false;
        if (found!=items_.end() && item.revision<=found->second.revision) return false;
        items_[item.item]=item; return true;
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
            if (peer.assigned) {
                peer.transfer_time += elapsed;
                if (peer.transfer_time >= 120) failed(peer, TransferFailure::Timeout);
            }
            bool keep = peer.transport->connected() || peer.transport->connecting();
            if ((!peer.ready && peer.elapsed >= 10) || (peer.rejected && peer.elapsed >= 1)) keep = false;
            if (keep && peer.transport->connected()) {
                keep=receive(peer);
                unsigned published=0;
                for (const auto& item : items_) if (keep && peer.ready && item.second.level==peer.level &&
                    peer.item_revisions[item.first]!=item.second.revision && peer.outgoing.size()<48 && published<8) {
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
