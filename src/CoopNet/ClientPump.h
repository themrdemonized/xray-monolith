#pragma once
#include "Roster.h"
#include "Transport.h"
#include "ActorSnapshot.h"
#include "ActorPresence.h"
#include "LevelAssignment.h"
#include "ActorInput.h"
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
    static constexpr double timeout_ = 10;
    void attach(std::unique_ptr<Transport> transport, const ClientHello& hello) {
        if (!transport) throw std::invalid_argument("Missing client transport");
        transport_ = std::move(transport); roster_.reset(); actors_ = {}; assignment_ = {}; assignments_ = {};
        level_ready_sent_ = false; transfer_failure_ = TransferFailure::None; sent_ = false; ready_sent_ = false; handshake_time_ = 0;
        hello_ = Frame{Message::ClientHello, Channel::Control, Delivery::ReliableOrdered, 1, encode_hello(hello)};
    }
    void lost() {
        if (assignment_.ticket) {
            transfer_failure_ = TransferFailure::Disconnected;
            if (transfer_failure_sink_) transfer_failure_sink_({session_.welcome().player,assignment_,transfer_failure_});
        }
        if (transport_) transport_->close();
        transport_.reset(); roster_.reset(); actors_ = {}; assignment_ = {}; session_.lost_connection();
    }
public:
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
            } else if (frame.message == Message::LevelAssignment) {
                LevelAssignment value;
                if (!decode_assignment(frame.payload, value) || frame.sequence != value.revision ||
                    !assignments_.accept(value.revision)) { lost(); return; }
                assignment_ = value; level_ready_sent_ = false; transfer_failure_ = TransferFailure::None;
            } else if (frame.message == Message::LevelCancelled) {
                LevelAssignment value; TransferFailure reason;
                if (!decode_cancellation(frame.payload, value, reason) || frame.sequence != value.revision ||
                    value.ticket != assignment_.ticket || value.level != assignment_.level ||
                    value.revision != assignment_.revision) { lost(); return; }
                assignment_ = {}; level_ready_sent_ = false; transfer_failure_ = reason;
                if (transfer_failure_sink_) transfer_failure_sink_({session_.welcome().player,value,reason});
            } else if (frame.message == Message::Disconnect) { lost(); return; }
            else { lost(); return; } // gameplay dispatcher is not installed yet
        }
    }
    void stop() {
        if (transport_) transport_->close();
        transport_.reset(); roster_.reset(); actors_ = {}; assignment_ = {}; transfer_failure_ = TransferFailure::None; session_.stop();
    }
};
}
