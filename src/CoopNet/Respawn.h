#pragma once
#include "Gameplay.h"
namespace coopnet {
enum class RespawnStatus : std::uint8_t { Accepted, Alive, NoLivingPlayer, Busy, Denied };
struct RespawnRequest { Identity actor=0; std::uint32_t generation=0,level=0,sequence=0; };
struct RespawnResult { RespawnRequest request; RespawnStatus status=RespawnStatus::Denied; std::uint32_t tick=0; std::array<float,3> position{}; };
inline bool valid_respawn_request(const RespawnRequest& request) {
    return request.actor && request.generation && request.level;
}
inline std::vector<std::uint8_t> encode_respawn_request(const RespawnRequest& request) {
    if (!valid_respawn_request(request)) throw std::invalid_argument("Invalid respawn request");
    Writer writer; writer.integer(request.actor,8); writer.integer(request.generation,4); writer.integer(request.level,4); writer.integer(request.sequence,4); return writer.bytes;
}
inline bool decode_respawn_request(const std::vector<std::uint8_t>& bytes,RespawnRequest& output) {
    Reader reader(bytes); RespawnRequest request; std::uint64_t generation,level,sequence;
    if (!reader.integer(request.actor,8) || !reader.integer(generation,4) || !reader.integer(level,4) || !reader.integer(sequence,4) || reader.remaining()) return false;
    request.generation=static_cast<std::uint32_t>(generation); request.level=static_cast<std::uint32_t>(level); request.sequence=static_cast<std::uint32_t>(sequence);
    if (!valid_respawn_request(request)) return false; output=request; return true;
}
inline bool valid_respawn_result(const RespawnResult& result) {
    if (!valid_respawn_request(result.request) || static_cast<unsigned>(result.status)>4) return false;
    for (const auto value:result.position) if (!std::isfinite(value) || std::abs(value)>1000000) return false;
    return true;
}
inline std::vector<std::uint8_t> encode_respawn_result(const RespawnResult& result) {
    if (!valid_respawn_result(result)) throw std::invalid_argument("Invalid respawn result");
    Writer writer; writer.bytes=encode_respawn_request(result.request); writer.integer(static_cast<unsigned>(result.status),1); writer.integer(result.tick,4);
    for (const auto value:result.position) write_float(writer,value); return writer.bytes;
}
inline bool decode_respawn_result(const std::vector<std::uint8_t>& bytes,RespawnResult& output) {
    if (bytes.size()!=37) return false;
    RespawnResult result; if (!decode_respawn_request({bytes.begin(),bytes.begin()+20},result.request)) return false;
    const std::vector<std::uint8_t> tail(bytes.begin()+20,bytes.end()); Reader remainder(tail); std::uint64_t status,tick;
    if (!remainder.integer(status,1) || !remainder.integer(tick,4)) return false;
    result.status=static_cast<RespawnStatus>(status); result.tick=static_cast<std::uint32_t>(tick);
    for (auto& value:result.position) if (!read_float(remainder,value)) return false;
    if (remainder.remaining() || !valid_respawn_result(result)) return false; output=result; return true;
}
}
