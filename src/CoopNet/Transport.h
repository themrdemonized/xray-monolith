#pragma once
#include "Protocol.h"
#include <array>
#include <deque>
#include <memory>
namespace coopnet {
enum class SendResult { Sent, Disconnected, Backpressure, Invalid };
class Transport {
public:
    virtual ~Transport() = default;
    virtual SendResult send(const Frame& frame) = 0;
    virtual bool receive(Frame& frame) = 0;
    virtual bool connected() const = 0;
    virtual bool connecting() const { return false; }
    virtual void close() = 0;
};
// Single-threaded deterministic test adapter, not a socket transport.
class MemoryTransport final : public Transport {
    struct Link {
        std::array<std::deque<std::vector<std::uint8_t>>, 2> queue;
        std::array<std::size_t, 2> bytes{};
        std::array<bool, 2> open{true, true};
    };
    std::shared_ptr<Link> link_;
    unsigned side_;
    MemoryTransport(std::shared_ptr<Link> link, unsigned side) : link_(std::move(link)), side_(side) {}
public:
    static std::pair<std::unique_ptr<MemoryTransport>, std::unique_ptr<MemoryTransport>> pair() {
        auto link = std::make_shared<Link>();
        return {std::unique_ptr<MemoryTransport>(new MemoryTransport(link, 0)),
            std::unique_ptr<MemoryTransport>(new MemoryTransport(link, 1))};
    }
    ~MemoryTransport() override { close(); }
    bool connected() const override { return link_->open[0] && link_->open[1]; }
    SendResult send(const Frame& frame) override {
        if (!connected()) return SendResult::Disconnected;
        if (!valid_contract(frame) || frame.payload.size() > max_payload - 16) return SendResult::Invalid;
        const auto target = 1 - side_;
        const auto size = frame.payload.size() + 16;
        if (link_->queue[target].size() >= 64 || size > 256 * 1024 - link_->bytes[target])
            return SendResult::Backpressure;
        auto bytes = encode(frame); link_->bytes[target] += bytes.size();
        link_->queue[target].push_back(std::move(bytes)); return SendResult::Sent;
    }
    bool receive(Frame& frame) override {
        if (!connected() || link_->queue[side_].empty()) return false;
        auto bytes = std::move(link_->queue[side_].front());
        link_->queue[side_].pop_front(); link_->bytes[side_] -= bytes.size(); return decode(bytes, frame);
    }
    void close() override {
        link_->open[side_] = false;
        for (unsigned i = 0; i < 2; ++i) { link_->queue[i].clear(); link_->bytes[i] = 0; }
    }
};
}
