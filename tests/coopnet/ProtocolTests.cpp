#include "../../src/CoopNet/Protocol.h"
#include <iostream>
#include <limits>

using namespace coopnet;
void require(bool condition) { if (!condition) throw std::runtime_error("CoopNet test failed"); }
int main() {
    Frame original{Message::ClientHello, Channel::Control, Delivery::ReliableOrdered, 42, {1,2,3}};
    auto bytes = encode(original);
    Frame result{};
    require(decode(bytes, result) && result.sequence == 42 && result.payload == original.payload);
    // Every possible truncation, including a complete header with missing payload.
    for (std::size_t n = 0; n < bytes.size(); ++n)
        require(!decode(std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + n), result));
    auto corrupt = bytes; corrupt[4] = 0; require(!decode(corrupt, result));
    corrupt = bytes; corrupt[6] = 255; require(!decode(corrupt, result));
    corrupt = bytes; corrupt[9] = 1; require(!decode(corrupt, result));
    corrupt = bytes; corrupt.push_back(0); require(!decode(corrupt, result));
    require(!decode(std::vector<std::uint8_t>(max_payload + 1), result));
    Frame largest{Message::ActorSnapshot, Channel::Actor, Delivery::UnreliableSequenced, 0,
        std::vector<std::uint8_t>(max_payload - 16, 17)};
    require(decode(encode(largest), result) && result.payload == largest.payload);
    largest.payload.push_back(1);
    bool rejected = false;
    try { encode(largest); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected);
    SequenceWindow sequence;
    require(sequence.accept(0xfffffffeu) && sequence.accept(0xffffffffu) && sequence.accept(0));
    require(!sequence.accept(0) && !sequence.accept(0xffffffffu) && sequence.accept(1));
    for (const unsigned fps : {30u, 60u, 144u}) {
        TickClock clock;
        unsigned total = 0;
        for (unsigned i = 0; i < fps * 10; ++i) total += clock.advance(1.0 / fps);
        require(total == 250);
        require(clock.advance(1000) == 5);
    }
    rejected = false;
    try { TickClock clock; clock.advance(std::numeric_limits<double>::infinity()); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected);
    std::cout << "CoopNet framing, malformed input, sequence wrap and tick tests passed\n";
}
