#pragma once
#include "Transport.h"
#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>
#include <set>

namespace coopnet {
// One runtime per process; poll and all adapter calls belong to its owner thread.
class GnsRuntime {
    inline static GnsRuntime* active_ = nullptr;
    ISteamNetworkingSockets* api_ = nullptr;
    HSteamListenSocket listener_ = k_HSteamListenSocket_Invalid;
    std::deque<HSteamNetConnection> pending_;
    std::set<HSteamNetConnection> connections_;
    static void changed(SteamNetConnectionStatusChangedCallback_t* event) {
        if (active_) active_->on_changed(*event);
    }
    void on_changed(const SteamNetConnectionStatusChangedCallback_t& event) {
        if (event.m_info.m_eState == k_ESteamNetworkingConnectionState_Connecting &&
            event.m_info.m_hListenSocket == listener_ && listener_ != k_HSteamListenSocket_Invalid) {
            if (connections_.size() >= 3 || api_->AcceptConnection(event.m_hConn) != k_EResultOK) {
                api_->CloseConnection(event.m_hConn, 0, "CoopNet capacity", false); return;
            }
            connections_.insert(event.m_hConn); pending_.push_back(event.m_hConn);
        }
        if (event.m_info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer ||
            event.m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally) {
            connections_.erase(event.m_hConn);
            for (auto it = pending_.begin(); it != pending_.end(); )
                if (*it == event.m_hConn) it = pending_.erase(it); else ++it;
            api_->CloseConnection(event.m_hConn, 0, nullptr, false);
        }
    }
public:
    GnsRuntime() {
        if (active_) throw std::logic_error("CoopNet GNS runtime already exists");
        SteamDatagramErrMsg error;
        if (!GameNetworkingSockets_Init(nullptr, error)) throw std::runtime_error(error);
        api_ = SteamNetworkingSockets(); active_ = this;
    }
    GnsRuntime(const GnsRuntime&) = delete;
    GnsRuntime& operator=(const GnsRuntime&) = delete;
    ~GnsRuntime() {
        for (auto connection : connections_) api_->CloseConnection(connection, 0, "Shutdown", false);
        if (listener_ != k_HSteamListenSocket_Invalid) api_->CloseListenSocket(listener_);
        active_ = nullptr; GameNetworkingSockets_Kill();
    }
    ISteamNetworkingSockets& api() { return *api_; }
    void poll() { api_->RunCallbacks(); }
    bool listen(std::uint16_t port) {
        if (!port || listener_ != k_HSteamListenSocket_Invalid) return false;
        SteamNetworkingIPAddr address; address.Clear(); address.m_port = port;
        SteamNetworkingConfigValue_t option;
        option.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, reinterpret_cast<void*>(changed));
        listener_ = api_->CreateListenSocketIP(address, 1, &option);
        return listener_ != k_HSteamListenSocket_Invalid;
    }
    HSteamNetConnection connect(const char* endpoint) {
        SteamNetworkingIPAddr address;
        if (!address.ParseString(endpoint) || !address.m_port) return k_HSteamNetConnection_Invalid;
        SteamNetworkingConfigValue_t option;
        option.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, reinterpret_cast<void*>(changed));
        auto connection = api_->ConnectByIPAddress(address, 1, &option);
        if (connection != k_HSteamNetConnection_Invalid) connections_.insert(connection);
        return connection;
    }
    HSteamNetConnection take_pending() {
        if (pending_.empty()) return k_HSteamNetConnection_Invalid;
        auto connection = pending_.front(); pending_.pop_front(); return connection;
    }
    void close(HSteamNetConnection connection) {
        connections_.erase(connection); api_->CloseConnection(connection, 0, "CoopNet disconnect", false);
    }
};
// Runtime must outlive each connection adapter.
class GnsTransport final : public Transport {
    GnsRuntime& runtime_;
    HSteamNetConnection connection_;
public:
    GnsTransport(GnsRuntime& runtime, HSteamNetConnection connection) : runtime_(runtime), connection_(connection) {}
    ~GnsTransport() override { close(); }
    bool connecting() const override {
        SteamNetConnectionInfo_t info;
        return connection_ != k_HSteamNetConnection_Invalid &&
            runtime_.api().GetConnectionInfo(connection_, &info) &&
            (info.m_eState == k_ESteamNetworkingConnectionState_Connecting ||
             info.m_eState == k_ESteamNetworkingConnectionState_FindingRoute);
    }
    bool connected() const override {
        SteamNetConnectionInfo_t info;
        return connection_ != k_HSteamNetConnection_Invalid &&
            runtime_.api().GetConnectionInfo(connection_, &info) &&
            info.m_eState == k_ESteamNetworkingConnectionState_Connected;
    }
    SendResult send(const Frame& frame) override {
        if (!connected()) return SendResult::Disconnected;
        if (!valid_contract(frame) || frame.payload.size() > max_payload - 16) return SendResult::Invalid;
        const auto bytes = encode(frame);
        const auto flags = frame.delivery == Delivery::ReliableOrdered ?
            k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_Unreliable;
        const auto result = runtime_.api().SendMessageToConnection(connection_, bytes.data(),
            static_cast<std::uint32_t>(bytes.size()), flags, nullptr);
        if (result == k_EResultOK) return SendResult::Sent;
        if (result == k_EResultLimitExceeded) return SendResult::Backpressure;
        return SendResult::Disconnected;
    }
    bool receive(Frame& frame) override {
        if (!connected()) return false;
        SteamNetworkingMessage_t* message = nullptr;
        if (runtime_.api().ReceiveMessagesOnConnection(connection_, &message, 1) != 1) return false;
        const bool valid_size = message->m_cbSize >= 16 && message->m_cbSize <= static_cast<int>(max_payload);
        bool valid = false;
        if (valid_size) {
            const auto* data = static_cast<const std::uint8_t*>(message->m_pData);
            valid = decode(std::vector<std::uint8_t>(data, data + message->m_cbSize), frame);
        }
        message->Release();
        if (!valid) close();
        return valid;
    }
    void close() override {
        if (connection_ != k_HSteamNetConnection_Invalid) {
            runtime_.close(connection_); connection_ = k_HSteamNetConnection_Invalid;
        }
    }
};
}
