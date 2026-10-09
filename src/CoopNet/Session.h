#pragma once
#include "Protocol.h"
#include <array>
namespace coopnet {
using Connection = std::uint64_t;
struct BuildIdentity { Identity game = 0; Identity mods = 0; };
struct ClientHello {
    std::uint16_t version = protocol_version;
    BuildIdentity build;
    Identity character = 0, resume_session = 0, resume_player = 0, resume_token = 0;
};
inline std::vector<std::uint8_t> encode_hello(const ClientHello& hello) {
    Writer writer;
    writer.integer(hello.version, 2);
    for (auto value : {hello.build.game, hello.build.mods, hello.character,
        hello.resume_session, hello.resume_player, hello.resume_token}) writer.integer(value, 8);
    return writer.bytes;
}
inline bool decode_hello(const std::vector<std::uint8_t>& bytes, ClientHello& hello) {
    Reader reader(bytes); std::uint64_t version; ClientHello value;
    if (!reader.integer(version, 2) || !reader.integer(value.build.game, 8) ||
        !reader.integer(value.build.mods, 8) || !reader.integer(value.character, 8) ||
        !reader.integer(value.resume_session, 8) || !reader.integer(value.resume_player, 8) ||
        !reader.integer(value.resume_token, 8) || reader.remaining()) return false;
    value.version = static_cast<std::uint16_t>(version); hello = value; return true;
}
struct Player {
    Identity id = 0, character = 0, resume_token = 0;
    Connection connection = 0;
    std::uint32_t generation = 0;
    bool connected = false;
};
enum class Admission { Accepted, WrongMode, Invalid, VersionMismatch, BuildMismatch,
    Full, DuplicateCharacter, DuplicateConnection, InvalidResume };
struct Welcome {
    Admission result = Admission::Invalid;
    Identity session = 0, player = 0, resume_token = 0;
    std::uint32_t generation = 0;
};
class HostSession {
    Mode mode_ = Mode::Offline;
    Identity session_ = 0;
    BuildIdentity build_;
    std::array<Player, 4> players_{};
    Identity next_player_ = 2;
public:
    Mode mode() const { return mode_; }
    const std::array<Player, 4>& players() const { return players_; }
    void stop() { mode_ = Mode::Offline; session_ = 0; players_ = {}; next_player_ = 2; }
    void start(Identity session, Identity character, BuildIdentity build) {
        if (!session || !character || !build.game || mode_ != Mode::Offline)
            throw std::invalid_argument("Invalid host session start");
        session_ = session; build_ = build;
        players_[0] = Player{1, character, 0, 0, 1, true}; mode_ = Mode::Host;
    }
    Welcome admit(Connection connection, const ClientHello& hello, Identity token) {
        auto reject = [](Admission reason) { return Welcome{reason, 0, 0, 0, 0}; };
        if (mode_ != Mode::Host) return reject(Admission::WrongMode);
        if (!connection || !hello.character) return reject(Admission::Invalid);
        if (hello.version != protocol_version) return reject(Admission::VersionMismatch);
        if (hello.build.game != build_.game || hello.build.mods != build_.mods)
            return reject(Admission::BuildMismatch);
        for (const auto& player : players_)
            if (player.connected && player.connection == connection) return reject(Admission::DuplicateConnection);
        if (hello.resume_session || hello.resume_player || hello.resume_token) {
            for (auto& player : players_) {
                if (player.id == hello.resume_player && player.id != 1 &&
                    player.character == hello.character && hello.resume_session == session_ &&
                    player.resume_token && player.resume_token == hello.resume_token && !player.connected) {
                    if (player.generation == UINT32_MAX) return reject(Admission::InvalidResume);
                    player.connection = connection; player.connected = true; ++player.generation;
                    return Welcome{Admission::Accepted, session_, player.id, player.resume_token, player.generation};
                }
            }
            return reject(Admission::InvalidResume);
        }
        for (const auto& player : players_)
            if (player.id && player.character == hello.character) return reject(Admission::DuplicateCharacter);
        if (!token) return reject(Admission::Invalid);
        for (const auto& player : players_)
            if (player.resume_token == token) return reject(Admission::Invalid);
        for (auto& player : players_) {
            if (player.id) continue;
            player = Player{next_player_++, hello.character, token, connection, 1, true};
            return Welcome{Admission::Accepted, session_, player.id, player.resume_token, player.generation};
        }
        return reject(Admission::Full);
    }
    bool disconnect(Connection connection) {
        if (!connection || mode_ != Mode::Host) return false;
        for (auto& player : players_) if (player.connected && player.connection == connection) {
            player.connected = false; player.connection = 0; return true;
        }
        return false;
    }
    // Release only after authoritative character checkpoint/export has succeeded.
    bool release(Identity id) {
        if (mode_ != Mode::Host || id == 0 || id == 1) return false;
        for (auto& player : players_) if (player.id == id && !player.connected) { player = {}; return true; }
        return false;
    }
};
inline std::vector<std::uint8_t> encode_welcome(const Welcome& welcome) {
    Writer writer;
    writer.integer(static_cast<unsigned>(welcome.result), 1);
    writer.integer(welcome.session, 8); writer.integer(welcome.player, 8);
    writer.integer(welcome.resume_token, 8); writer.integer(welcome.generation, 4);
    return writer.bytes;
}
inline bool decode_welcome(const std::vector<std::uint8_t>& bytes, Welcome& output) {
    Reader reader(bytes); Welcome value; std::uint64_t result, generation;
    if (!reader.integer(result, 1) || result > static_cast<unsigned>(Admission::InvalidResume) ||
        !reader.integer(value.session, 8) || !reader.integer(value.player, 8) ||
        !reader.integer(value.resume_token, 8) || !reader.integer(generation, 4) || reader.remaining()) return false;
    value.result = static_cast<Admission>(result); value.generation = static_cast<std::uint32_t>(generation);
    if (value.result == Admission::Accepted) {
        if (!value.session || value.player < 2 || !value.resume_token || !value.generation) return false;
    } else if (value.session || value.player || value.resume_token || value.generation) return false;
    output = value; return true;
}
enum class ClientState { Offline, Connecting, Connected, Disconnected, Rejected };
class ClientSession {
    ClientState state_ = ClientState::Offline;
    ClientHello hello_;
    Welcome welcome_;
public:
    ClientState state() const { return state_; }
    const Welcome& welcome() const { return welcome_; }
    ClientHello begin(Identity character, BuildIdentity build) {
        if (!character || !build.game || state_ != ClientState::Offline)
            throw std::invalid_argument("Invalid client session start");
        hello_ = ClientHello{protocol_version, build, character, 0, 0, 0};
        state_ = ClientState::Connecting; return hello_;
    }
    ClientHello begin_saved(Identity character, BuildIdentity build,const Welcome& previous) {
        if (previous.result!=Admission::Accepted || !previous.session || previous.player<2 || !previous.resume_token || !previous.generation)
            throw std::invalid_argument("Invalid saved session");
        begin(character,build); welcome_=previous;
        hello_.resume_session=previous.session; hello_.resume_player=previous.player; hello_.resume_token=previous.resume_token;
        return hello_;
    }
    bool accept(const std::vector<std::uint8_t>& payload) {
        if (state_ != ClientState::Connecting) return false;
        Welcome value;
        if (!decode_welcome(payload, value)) return false;
        if (value.result == Admission::Accepted && hello_.resume_session &&
            (value.session != hello_.resume_session || value.player != hello_.resume_player ||
             value.resume_token != hello_.resume_token || value.generation <= welcome_.generation)) return false;
        welcome_ = value;
        state_ = value.result == Admission::Accepted ? ClientState::Connected : ClientState::Rejected;
        return true;
    }
    void lost_connection() {
        if (state_ == ClientState::Connected) state_ = ClientState::Disconnected;
        else if (state_ == ClientState::Connecting)
            state_ = hello_.resume_session ? ClientState::Disconnected : ClientState::Offline;
    }
    ClientHello reconnect() {
        if (state_ != ClientState::Disconnected) throw std::logic_error("No resumable session");
        hello_.resume_session = welcome_.session; hello_.resume_player = welcome_.player;
        hello_.resume_token = welcome_.resume_token; state_ = ClientState::Connecting; return hello_;
    }
    void stop() { state_ = ClientState::Offline; hello_ = {}; welcome_ = {}; }
};
}
