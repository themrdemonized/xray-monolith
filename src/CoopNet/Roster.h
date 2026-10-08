#pragma once
#include "Session.h"
namespace coopnet {
struct Participant {
    Identity player = 0, character = 0;
    std::uint32_t generation = 0;
    bool connected = false;
};
struct Roster {
    Identity session = 0;
    std::uint32_t revision = 0;
    std::vector<Participant> participants;
};
inline bool valid_roster(const Roster& roster) {
    if (!roster.session || roster.participants.empty() || roster.participants.size() > 4) return false;
    unsigned hosts = 0;
    for (std::size_t i = 0; i < roster.participants.size(); ++i) {
        const auto& entry = roster.participants[i];
        if (!entry.player || !entry.character || !entry.generation) return false;
        if (entry.player == 1) { ++hosts; if (!entry.connected) return false; }
        for (std::size_t j = 0; j < i; ++j)
            if (roster.participants[j].player == entry.player ||
                roster.participants[j].character == entry.character) return false;
    }
    return hosts == 1;
}
inline std::vector<std::uint8_t> encode_roster(const Roster& roster) {
    if (!valid_roster(roster)) throw std::invalid_argument("Invalid roster");
    Writer writer;
    writer.integer(roster.session, 8); writer.integer(roster.revision, 4);
    writer.integer(roster.participants.size(), 1);
    for (const auto& entry : roster.participants) {
        writer.integer(entry.player, 8); writer.integer(entry.character, 8);
        writer.integer(entry.generation, 4); writer.integer(entry.connected ? 1 : 0, 1);
    }
    return writer.bytes;
}
inline bool decode_roster(const std::vector<std::uint8_t>& bytes, Roster& output) {
    Reader reader(bytes); Roster value; std::uint64_t revision, count;
    if (!reader.integer(value.session, 8) || !reader.integer(revision, 4) ||
        !reader.integer(count, 1) || count == 0 || count > 4 || reader.remaining() != count * 21) return false;
    value.revision = static_cast<std::uint32_t>(revision);
    for (std::uint64_t i = 0; i < count; ++i) {
        Participant entry; std::uint64_t generation, connected;
        if (!reader.integer(entry.player, 8) || !reader.integer(entry.character, 8) ||
            !reader.integer(generation, 4) || !reader.integer(connected, 1) || connected > 1) return false;
        entry.generation = static_cast<std::uint32_t>(generation); entry.connected = connected != 0;
        value.participants.push_back(entry);
    }
    if (!valid_roster(value)) return false;
    output = std::move(value); return true;
}
inline Roster make_roster(const HostSession& host, Identity session, std::uint32_t revision) {
    Roster roster{session, revision, {}};
    for (const auto& entry : host.players())
        if (entry.id) roster.participants.push_back({entry.id, entry.character, entry.generation, entry.connected});
    if (host.mode() != Mode::Host || !valid_roster(roster)) throw std::logic_error("No active host roster");
    return roster;
}
class ClientRoster {
    Identity session_, player_;
    SequenceWindow revisions_;
    Roster roster_;
public:
    ClientRoster(Identity session, Identity player) : session_(session), player_(player) {}
    const Roster& current() const { return roster_; }
    bool apply(const std::vector<std::uint8_t>& payload) {
        Roster value;
        if (!decode_roster(payload, value) || value.session != session_) return false;
        bool present = false;
        for (const auto& entry : value.participants) if (entry.player == player_) present = entry.connected;
        if (!present || !revisions_.accept(value.revision)) return false;
        roster_ = std::move(value); return true;
    }
};
}
