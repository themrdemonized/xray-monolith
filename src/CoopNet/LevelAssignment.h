#pragma once
#include "Protocol.h"
namespace coopnet {
struct LevelAssignment {
    Identity ticket = 0;
    std::uint32_t level = 0, revision = 0;
};
enum class TransferFailure : std::uint8_t { None, Timeout, Cancelled, Disconnected };
struct LevelFailure { Identity player = 0; LevelAssignment assignment; TransferFailure reason = TransferFailure::None; };
inline std::vector<std::uint8_t> encode_assignment(const LevelAssignment& value) {
    if (!value.ticket || !value.level) throw std::invalid_argument("Invalid level assignment");
    Writer writer; writer.integer(value.ticket, 8); writer.integer(value.level, 4);
    writer.integer(value.revision, 4); return writer.bytes;
}
inline bool decode_assignment(const std::vector<std::uint8_t>& bytes, LevelAssignment& output) {
    Reader reader(bytes); LevelAssignment value; std::uint64_t level, revision;
    if (!reader.integer(value.ticket, 8) || !reader.integer(level, 4) || !reader.integer(revision, 4) ||
        reader.remaining() || !value.ticket || !level) return false;
    value.level = static_cast<std::uint32_t>(level); value.revision = static_cast<std::uint32_t>(revision);
    output = value; return true;
}
inline std::vector<std::uint8_t> encode_cancellation(const LevelAssignment& assignment, TransferFailure reason) {
    if (reason != TransferFailure::Timeout && reason != TransferFailure::Cancelled)
        throw std::invalid_argument("Invalid cancellation reason");
    auto bytes = encode_assignment(assignment); bytes.push_back(static_cast<std::uint8_t>(reason)); return bytes;
}
inline bool decode_cancellation(const std::vector<std::uint8_t>& bytes, LevelAssignment& assignment, TransferFailure& reason) {
    if (bytes.size() != 17 || bytes.back() < 1 || bytes.back() > 2) return false;
    if (!decode_assignment({bytes.begin(),bytes.end() - 1}, assignment)) return false;
    reason = static_cast<TransferFailure>(bytes.back()); return true;
}
}
