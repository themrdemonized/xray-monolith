#pragma once
#include <cstdint>
#include <vector>
#include <stdexcept>
#include <cmath>
#include <utility>

namespace coopnet {
constexpr std::uint16_t protocol_version = 8;
constexpr std::size_t max_payload = 16384;
enum class Mode { Offline, Host, Client };
enum class Channel : std::uint8_t { Control, Actor, Combat, Inventory, World, AI, Transition };
enum class Delivery : std::uint8_t { ReliableOrdered, UnreliableSequenced };
enum class Message : std::uint16_t { ClientHello = 1, ServerHello, Disconnect, ActorSnapshot, LevelReady, Roster, ClientReady, ActorCreate, ActorRemove, LevelAssignment, LevelCancelled, ActorInput, InventoryRequest, InventoryResult, ItemState, ActorVitals, WorldBaseline, WorldChunk, WorldReceived };
using Identity = std::uint64_t;
struct Frame {
    Message message;
    Channel channel;
    Delivery delivery;
    std::uint32_t sequence;
    std::vector<std::uint8_t> payload;
};
class Writer {
public:
    std::vector<std::uint8_t> bytes;
    void integer(std::uint64_t value, unsigned width) {
        if (width == 0 || width > 8 || bytes.size() + width > max_payload)
            throw std::length_error("CoopNet payload limit");
        for (unsigned i = 0; i < width; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    }
};
class Reader {
    const std::vector<std::uint8_t>& bytes;
    std::size_t offset = 0;
public:
    explicit Reader(const std::vector<std::uint8_t>& value) : bytes(value) {}
    bool integer(std::uint64_t& value, unsigned width) {
        if (width == 0 || width > 8 || width > bytes.size() - offset) return false;
        value = 0;
        for (unsigned i = 0; i < width; ++i) value |= std::uint64_t(bytes[offset++]) << (8 * i);
        return true;
    }
    std::size_t remaining() const { return bytes.size() - offset; }
};
inline bool valid_contract(const Frame& frame) {
    if (frame.message==Message::WorldBaseline || frame.message==Message::WorldChunk || frame.message==Message::WorldReceived)
        return frame.channel==Channel::World && frame.delivery==Delivery::ReliableOrdered;
    if (frame.message == Message::InventoryRequest || frame.message == Message::InventoryResult || frame.message == Message::ItemState)
        return frame.channel == Channel::Inventory && frame.delivery == Delivery::ReliableOrdered;
    if (frame.message == Message::ActorVitals)
        return frame.channel == Channel::Combat && frame.delivery == Delivery::UnreliableSequenced;
    if (frame.message == Message::ActorCreate || frame.message == Message::ActorRemove)
        return frame.channel == Channel::World && frame.delivery == Delivery::ReliableOrdered;
    if (frame.message == Message::ActorSnapshot || frame.message == Message::ActorInput)
        return frame.channel == Channel::Actor && frame.delivery == Delivery::UnreliableSequenced;
    if (frame.message == Message::LevelReady || frame.message == Message::LevelAssignment || frame.message == Message::LevelCancelled)
        return frame.channel == Channel::Transition && frame.delivery == Delivery::ReliableOrdered;
    return (frame.message == Message::ClientHello || frame.message == Message::ServerHello ||
        frame.message == Message::Disconnect || frame.message == Message::Roster ||
        frame.message == Message::ClientReady) && frame.channel == Channel::Control &&
        frame.delivery == Delivery::ReliableOrdered;
}
inline std::vector<std::uint8_t> encode(const Frame& frame) {
    if (!valid_contract(frame) || frame.payload.size() > max_payload - 16)
        throw std::invalid_argument("Invalid CoopNet frame");
    Writer writer;
    writer.integer(0x504f4f43, 4); // COOP, little endian
    writer.integer(protocol_version, 2);
    writer.integer(static_cast<unsigned>(frame.message), 2);
    writer.integer(static_cast<unsigned>(frame.channel), 1);
    writer.integer(static_cast<unsigned>(frame.delivery), 1);
    writer.integer(frame.sequence, 4);
    writer.integer(frame.payload.size(), 2);
    writer.bytes.insert(writer.bytes.end(), frame.payload.begin(), frame.payload.end());
    return writer.bytes;
}
inline bool decode(const std::vector<std::uint8_t>& bytes, Frame& output) {
    if (bytes.size() < 16 || bytes.size() > max_payload) return false;
    Reader reader(bytes);
    std::uint64_t magic, version, type, channel, delivery, sequence, length;
    if (!reader.integer(magic, 4) || magic != 0x504f4f43 ||
        !reader.integer(version, 2) || version != protocol_version ||
        !reader.integer(type, 2) || !reader.integer(channel, 1) ||
        !reader.integer(delivery, 1) || !reader.integer(sequence, 4) ||
        !reader.integer(length, 2) || length != reader.remaining()) return false;
    Frame frame{static_cast<Message>(type), static_cast<Channel>(channel),
        static_cast<Delivery>(delivery), static_cast<std::uint32_t>(sequence),
        std::vector<std::uint8_t>(bytes.begin() + 16, bytes.end())};
    if (!valid_contract(frame)) return false;
    output = std::move(frame);
    return true;
}
class SequenceWindow {
    bool initialized = false;
    std::uint32_t last = 0;
public:
    bool accept(std::uint32_t sequence) {
        const auto distance = sequence - last;
        if (initialized && (distance == 0 || distance >= 0x80000000u)) return false;
        initialized = true;
        last = sequence;
        return true;
    }
};
class TickClock {
    double step;
    double accumulated = 0;
public:
    explicit TickClock(unsigned hz = 25) {
        if (hz < 20 || hz > 30) throw std::invalid_argument("Tick rate must be 20-30 Hz");
        step = 1.0 / hz;
    }
    unsigned advance(double elapsed) {
        if (!std::isfinite(elapsed) || elapsed < 0) throw std::invalid_argument("Invalid elapsed time");
        accumulated += elapsed;
        if (accumulated > step * 5) accumulated = step * 5;
        const auto ticks = static_cast<unsigned>((accumulated + 1e-12) / step);
        accumulated -= ticks * step;
        if (accumulated < 0) accumulated = 0;
        return ticks;
    }
};
}
