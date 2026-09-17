#pragma once
#include <naval_sdk/bot.hpp>
#include <memory>

namespace naval_sdk::detail {
struct Endpoint { bool tls = false; std::string host, port, target, authority; };
Endpoint parse_url(const std::string& url);
class Transport {
public:
    Transport(const Endpoint& endpoint, const RunOptions& options);
    ~Transport();
    void send(const std::string& frame);
    std::string receive(bool& text, double timeout = 0);
    void close();
    std::optional<int> close_code() const;
    std::string close_reason() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
class ConnectionClosed : public std::runtime_error {
public:
    ConnectionClosed() : std::runtime_error("connection closed") {}
};
}
