#pragma once
#include "Protocol.h"
#include <algorithm>
namespace coopnet {
enum class PartyStage : std::uint8_t { Idle, Gathering, Loading, Arrived, Failed };
struct PartyStatus {
    std::uint32_t revision=0, destination=0;
    PartyStage stage=PartyStage::Idle;
    std::uint8_t present=0, required=0;
};
inline bool valid_party_status(const PartyStatus& value) {
    return static_cast<unsigned>(value.stage)<=4 && value.required<=4 && value.present<=value.required &&
        (value.stage==PartyStage::Idle || (value.destination && value.required));
}
inline std::vector<std::uint8_t> encode_party_status(const PartyStatus& value) {
    if (!valid_party_status(value)) throw std::invalid_argument("Invalid party transition status");
    Writer writer; writer.integer(value.revision,4); writer.integer(value.destination,4);
    writer.integer(static_cast<unsigned>(value.stage),1); writer.integer(value.present,1); writer.integer(value.required,1);
    return writer.bytes;
}
inline bool decode_party_status(const std::vector<std::uint8_t>& bytes,PartyStatus& output) {
    Reader reader(bytes); std::uint64_t revision,destination,stage,present,required;
    if (!reader.integer(revision,4) || !reader.integer(destination,4) || !reader.integer(stage,1) ||
        !reader.integer(present,1) || !reader.integer(required,1) || reader.remaining()) return false;
    PartyStatus value{static_cast<std::uint32_t>(revision),static_cast<std::uint32_t>(destination),
        static_cast<PartyStage>(stage),static_cast<std::uint8_t>(present),static_cast<std::uint8_t>(required)};
    if (!valid_party_status(value)) return false; output=value; return true;
}
// Called only with host-observed occupancy. No guest readiness claims enter this barrier.
class PartyBarrier {
    Identity exit_=0;
    double held_=0;
    std::uint8_t required_=0;
public:
    void reset() { exit_=0; held_=0; required_=0; }
    bool update(Identity exit,unsigned present,unsigned required,double elapsed) {
        if (!std::isfinite(elapsed) || elapsed<0 || required>4 || present>required)
            throw std::invalid_argument("Invalid party occupancy");
        if (!exit || !required || present!=required) { reset(); return false; }
        if (exit_!=exit || required_!=required) { exit_=exit; required_=static_cast<std::uint8_t>(required); held_=0; }
        // A long frame cannot satisfy the dwell by itself.
        held_+=(std::min)(elapsed,.1);
        return held_>=1;
    }
};
}
