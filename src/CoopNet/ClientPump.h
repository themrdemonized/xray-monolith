#pragma once
#include "Roster.h"
#include "Transport.h"
#include "ActorSnapshot.h"
#include "ActorPresence.h"
#include "LevelAssignment.h"
#include "ActorInput.h"
#include "Gameplay.h"
#include "WorldBaseline.h"
#include <functional>
#include <memory>
namespace coopnet {
// Engine-safe owner-thread pump; no transport callbacks access gameplay objects.
class ClientPump {
    ClientSession session_;
    std::unique_ptr<Transport> transport_;
    std::unique_ptr<ClientRoster> roster_;
    Frame hello_{};
    bool sent_ = false;
    bool ready_sent_ = false;
    double handshake_time_ = 0;
    std::function<void(const ActorSnapshot&)> snapshot_sink_;
    ActorReplicas actors_;
    LevelAssignment assignment_;
    SequenceWindow assignments_;
    bool level_ready_sent_ = false;
    TransferFailure transfer_failure_ = TransferFailure::None;
    std::function<void(const LevelFailure&)> transfer_failure_sink_;
    std::map<Identity,ItemState> items_;
    std::map<std::uint32_t,InventoryRequest> pending_inventory_;
    std::map<std::uint32_t,std::pair<InventoryRequest,Identity>> inventory_history_;
    SequenceWindow inventory_sequences_;
    std::map<Identity,std::pair<std::uint32_t,SequenceWindow>> vitals_sequences_;
    std::function<void(const InventoryResult&)> inventory_sink_;
    std::function<void(const ActorVitals&)> vitals_sink_;
    BaselineAssembly baseline_assembly_;
    WorldBaseline baseline_;
    bool baseline_validated_=false, baseline_acknowledged_=false;
    double baseline_time_=0;
    std::function<bool(const WorldBaseline&,const std::vector<std::uint8_t>&)> baseline_sink_;
    std::function<void(const WorldBaseline&,std::uint32_t)> baseline_progress_sink_;
    void clear_baseline() {
        baseline_assembly_.clear(); baseline_={}; baseline_validated_=false; baseline_acknowledged_=false; baseline_time_=0;
    }
    static constexpr double timeout_ = 10;
    void attach(std::unique_ptr<Transport> transport, const ClientHello& hello) {
        if (!transport) throw std::invalid_argument("Missing client transport");
        transport_ = std::move(transport); roster_.reset(); actors_ = {}; assignment_ = {}; assignments_ = {};
        level_ready_sent_ = false; transfer_failure_ = TransferFailure::None; sent_ = false; ready_sent_ = false; handshake_time_ = 0;
        items_.clear(); vitals_sequences_.clear(); pending_inventory_.clear();
        inventory_history_.clear(); inventory_sequences_={};
        clear_baseline();
        hello_ = Frame{Message::ClientHello, Channel::Control, Delivery::ReliableOrdered, 1, encode_hello(hello)};
    }
    void lost() {
        if (assignment_.ticket) {
            transfer_failure_ = TransferFailure::Disconnected;
            if (transfer_failure_sink_) transfer_failure_sink_({session_.welcome().player,assignment_,transfer_failure_});
        }
        if (transport_) transport_->close();
        transport_.reset(); roster_.reset(); actors_ = {}; assignment_ = {}; session_.lost_connection();
        items_.clear(); vitals_sequences_.clear(); pending_inventory_.clear();
        clear_baseline();
    }
public:
    const WorldBaseline& baseline() const { return baseline_; }
    bool baseline_acknowledged() const { return baseline_acknowledged_; }
    void set_baseline_sink(std::function<bool(const WorldBaseline&,const std::vector<std::uint8_t>&)> sink) {
        baseline_sink_=std::move(sink);
    }
    void set_baseline_progress_sink(std::function<void(const WorldBaseline&,std::uint32_t)> sink) {
        baseline_progress_sink_=std::move(sink);
    }
    bool acknowledge_baseline() {
        if (!transport_ || session_.state()!=ClientState::Connected || !baseline_validated_ || baseline_acknowledged_) return false;
        const auto result=transport_->send({Message::WorldReceived,Channel::World,Delivery::ReliableOrdered,0,encode_baseline(baseline_)});
        if (result==SendResult::Sent) { baseline_acknowledged_=true; return true; }
        if (result!=SendResult::Backpressure) lost();
        return false;
    }
    const std::map<Identity,ItemState>& items() const { return items_; }
    void set_inventory_sink(std::function<void(const InventoryResult&)> sink) { inventory_sink_=std::move(sink); }
    void set_vitals_sink(std::function<void(const ActorVitals&)> sink) { vitals_sink_=std::move(sink); }
    SendResult send_inventory(const InventoryRequest& request) {
        if (!transport_ || session_.state()!=ClientState::Connected || !level_ready_sent_) return SendResult::Disconnected;
        const auto* actor=actors_.find(request.actor);
        if (!valid_inventory_request(request) || !actor || actor->player!=session_.welcome().player ||
            actor->generation!=request.generation || actor->level!=request.level || assignment_.level!=request.level)
            return SendResult::Invalid;
        const auto history=inventory_history_.find(request.sequence);
        auto sequences=inventory_sequences_;
        if (history!=inventory_history_.end()) {
            if (history->second.second!=assignment_.ticket ||
                encode_inventory_request(history->second.first)!=encode_inventory_request(request)) return SendResult::Invalid;
        } else if (!sequences.accept(request.sequence)) return SendResult::Invalid;
        const auto pending=pending_inventory_.find(request.sequence);
        if (pending==pending_inventory_.end() && pending_inventory_.size()>=64) return SendResult::Backpressure;
        const auto result=transport_->send({Message::InventoryRequest,Channel::Inventory,Delivery::ReliableOrdered,
            request.sequence,encode_inventory_request(request)});
        if (result==SendResult::Sent) {
            if (history==inventory_history_.end()) {
                if (inventory_history_.size()>=256) {
                    for (auto old=inventory_history_.begin();old!=inventory_history_.end();++old) {
                        if (!pending_inventory_.count(old->first)) { inventory_history_.erase(old); break; }
                    }
                }
                inventory_history_[request.sequence]={request,assignment_.ticket};
                inventory_sequences_=sequences;
            }
            pending_inventory_[request.sequence]=request;
        }
        if (result!=SendResult::Sent && result!=SendResult::Backpressure) lost();
        return result;
    }
    SendResult send_input(const ActorInput& input) {
        if (!transport_ || session_.state() != ClientState::Connected || !level_ready_sent_)
            return SendResult::Disconnected;
        const auto* actor = actors_.find(input.entity);
        if (!valid_input(input) || !actor || actor->player != session_.welcome().player ||
            actor->generation != input.generation || actor->level != input.level || assignment_.level != input.level)
            return SendResult::Invalid;
        const auto result = transport_->send(Frame{Message::ActorInput, Channel::Actor, Delivery::UnreliableSequenced,
            input.sequence, encode_input(input)});
        if (result != SendResult::Sent && result != SendResult::Backpressure) lost();
        return result;
    }
    const ClientSession& session() const { return session_; }
    const ClientRoster* roster() const { return roster_.get(); }
    const ActorReplicas& actors() const { return actors_; }
    const LevelAssignment& assignment() const { return assignment_; }
    TransferFailure transfer_failure() const { return transfer_failure_; }
    void set_transfer_failure_sink(std::function<void(const LevelFailure&)> sink) { transfer_failure_sink_ = std::move(sink); }
    bool acknowledge_level(std::uint32_t loaded_level) {
        if (!transport_ || session_.state() != ClientState::Connected || level_ready_sent_ ||
            (baseline_.id && !baseline_acknowledged_) ||
            !assignment_.ticket || loaded_level != assignment_.level) return false;
        const auto result = transport_->send(Frame{Message::LevelReady, Channel::Transition, Delivery::ReliableOrdered,
            assignment_.revision, encode_assignment(assignment_)});
        if (result == SendResult::Sent) { level_ready_sent_ = true; return true; }
        if (result != SendResult::Backpressure) lost();
        return false;
    }
    // The engine adapter owns entity bindings and rejects stale generations/levels.
    void set_snapshot_sink(std::function<void(const ActorSnapshot&)> sink) { snapshot_sink_ = std::move(sink); }
    void start(std::unique_ptr<Transport> transport, Identity character, BuildIdentity build) {
        if (!transport) throw std::invalid_argument("Missing client transport");
        attach(std::move(transport), session_.begin(character, build));
    }
    void reconnect(std::unique_ptr<Transport> transport) {
        if (!transport) throw std::invalid_argument("Missing reconnect transport");
        attach(std::move(transport), session_.reconnect());
    }
    void update(double elapsed) {
        if (!std::isfinite(elapsed) || elapsed < 0) throw std::invalid_argument("Invalid pump time");
        if (!transport_) return;
        if (baseline_.id && !baseline_acknowledged_) {
            baseline_time_+=elapsed;
            if (baseline_time_>=120) { lost(); return; }
        }
        if (session_.state() == ClientState::Connecting) {
            handshake_time_ += elapsed;
            if (handshake_time_ >= timeout_) { lost(); return; }
        }
        if (!transport_->connected()) {
            if (!transport_->connecting()) lost();
            return;
        }
        if (!sent_) {
            const auto result = transport_->send(hello_);
            if (result == SendResult::Backpressure) return;
            if (result != SendResult::Sent) { lost(); return; }
            sent_ = true;
        }
        for (unsigned messages = 0; messages < 32; ++messages) {
            if (session_.state() == ClientState::Connected && !ready_sent_) {
                const auto result = transport_->send(Frame{Message::ClientReady, Channel::Control,
                    Delivery::ReliableOrdered, 2, {}});
                if (result == SendResult::Backpressure) return;
                if (result != SendResult::Sent) { lost(); return; }
                ready_sent_ = true;
            }
            Frame frame{};
            if (!transport_->receive(frame)) break;
            if (session_.state() == ClientState::Connecting) {
                if (frame.message != Message::ServerHello || !session_.accept(frame.payload)) { lost(); return; }
                if (session_.state() == ClientState::Rejected) { transport_->close(); transport_.reset(); return; }
                const auto& welcome = session_.welcome();
                roster_ = std::make_unique<ClientRoster>(welcome.session, welcome.player);
            } else if (frame.message == Message::Roster) {
                if (!roster_->apply(frame.payload)) { lost(); return; }
            } else if (frame.message==Message::WorldBaseline) {
                WorldBaseline value;
                if (!decode_baseline(frame.payload,value) || frame.sequence ||
                    (baseline_.id && (!baseline_acknowledged_ || value.id==baseline_.id))) { lost(); return; }
                clear_baseline(); baseline_=value;
                if (!baseline_assembly_.begin(value)) { lost(); return; }
                if (baseline_progress_sink_) baseline_progress_sink_(baseline_,0);
                level_ready_sent_=false; items_.clear(); pending_inventory_.clear();
            } else if (frame.message==Message::WorldChunk) {
                WorldChunk chunk;
                if (!decode_world_chunk(frame.payload,chunk) || frame.sequence!=chunk.offset/baseline_chunk_bytes ||
                    !baseline_assembly_.append(chunk)) { lost(); return; }
                if (baseline_progress_sink_) baseline_progress_sink_(baseline_,static_cast<std::uint32_t>(baseline_assembly_.bytes().size()));
                if (baseline_assembly_.complete()) {
                    if (!baseline_sink_ || !baseline_sink_(baseline_,baseline_assembly_.bytes())) { lost(); return; }
                    baseline_validated_=true; baseline_assembly_.clear();
                }
            } else if (frame.message == Message::ActorSnapshot) {
                ActorSnapshot snapshot;
                if (!decode_snapshot(frame.payload, snapshot) || frame.sequence != snapshot.tick) { lost(); return; }
                if (actors_.push(snapshot) && snapshot_sink_) snapshot_sink_(snapshot);
            } else if (frame.message == Message::ActorCreate || frame.message == Message::ActorRemove) {
                ActorPresence presence;
                if (!decode_presence(frame.payload, presence)) { lost(); return; }
                bool participant = false;
                for (const auto& entry : roster_->current().participants)
                    if (entry.player == presence.player && entry.character == presence.character) participant = true;
                if (!participant || !(frame.message == Message::ActorCreate ?
                    actors_.create(presence) : actors_.remove(presence))) { lost(); return; }
                if (frame.message==Message::ActorRemove || frame.message==Message::ActorCreate) {
                    if (frame.message==Message::ActorRemove) vitals_sequences_.erase(presence.entity);
                    for (auto pending=pending_inventory_.begin();pending!=pending_inventory_.end();) {
                        if (pending->second.actor==presence.entity && (frame.message==Message::ActorRemove ||
                            pending->second.generation!=presence.generation || pending->second.level!=presence.level))
                            pending=pending_inventory_.erase(pending);
                        else ++pending;
                    }
                }
            } else if (frame.message==Message::ItemState) {
                ItemState item;
                if (!decode_item_state(frame.payload,item) || item.revision!=frame.sequence) { lost(); return; }
                if (!level_ready_sent_ || item.level!=assignment_.level) continue;
                auto found=items_.find(item.item);
                if (found==items_.end() && items_.size()>=4096) { lost(); return; }
                if (found==items_.end() || item.revision>found->second.revision) items_[item.item]=std::move(item);
            } else if (frame.message==Message::InventoryResult) {
                InventoryResult result;
                if (!decode_inventory_result(frame.payload,result) || result.sequence!=frame.sequence) { lost(); return; }
                const auto pending=pending_inventory_.find(result.sequence);
                if (pending==pending_inventory_.end()) continue; // unsolicited, duplicate or retired binding
                const auto request=pending->second;
                if (request.item!=result.item) { lost(); return; }
                pending_inventory_.erase(pending);
                const auto* actor=actors_.find(request.actor);
                if (!actor || actor->generation!=request.generation || actor->level!=request.level ||
                    actor->player!=session_.welcome().player || !level_ready_sent_ || assignment_.level!=request.level) continue;
                if (inventory_sink_) inventory_sink_(result);
            } else if (frame.message==Message::ActorVitals) {
                ActorVitals vitals;
                if (!decode_vitals(frame.payload,vitals) || vitals.tick!=frame.sequence) { lost(); return; }
                const auto* actor=actors_.find(vitals.actor);
                if (!actor || actor->generation!=vitals.generation || actor->level!=vitals.level ||
                    !level_ready_sent_ || assignment_.level!=vitals.level) continue;
                auto& sequence=vitals_sequences_[vitals.actor];
                if (sequence.first!=vitals.generation) sequence={vitals.generation,{}};
                if (sequence.second.accept(vitals.tick) && vitals_sink_) vitals_sink_(vitals);
            } else if (frame.message == Message::LevelAssignment) {
                LevelAssignment value;
                if (!decode_assignment(frame.payload, value) || frame.sequence != value.revision ||
                    !assignments_.accept(value.revision)) { lost(); return; }
                assignment_ = value; level_ready_sent_ = false; transfer_failure_ = TransferFailure::None;
                items_.clear(); vitals_sequences_.clear(); pending_inventory_.clear();
            } else if (frame.message == Message::LevelCancelled) {
                LevelAssignment value; TransferFailure reason;
                if (!decode_cancellation(frame.payload, value, reason) || frame.sequence != value.revision ||
                    value.ticket != assignment_.ticket || value.level != assignment_.level ||
                    value.revision != assignment_.revision) { lost(); return; }
                assignment_ = {}; level_ready_sent_ = false; transfer_failure_ = reason;
                items_.clear(); vitals_sequences_.clear(); pending_inventory_.clear();
                if (transfer_failure_sink_) transfer_failure_sink_({session_.welcome().player,value,reason});
            } else if (frame.message == Message::Disconnect) { lost(); return; }
            else { lost(); return; } // gameplay dispatcher is not installed yet
        }
    }
    void stop() {
        if (transport_) transport_->close();
        transport_.reset(); roster_.reset(); actors_ = {}; assignment_ = {}; transfer_failure_ = TransferFailure::None; session_.stop();
        items_.clear(); vitals_sequences_.clear(); pending_inventory_.clear();
        inventory_history_.clear(); inventory_sequences_={};
        clear_baseline();
    }
};
}
