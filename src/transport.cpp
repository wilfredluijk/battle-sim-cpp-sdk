#include "transport.hpp"
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <openssl/ssl.h>
#include <algorithm>
#include <cctype>
#include <chrono>

namespace naval_sdk::detail {
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;
using Error = boost::system::error_code;

Endpoint parse_url(const std::string& url) {
    auto fail = []() -> void { throw std::invalid_argument("server URL must be ws:// or wss:// with no credentials, query or fragment"); };
    Endpoint e;
    std::size_t start;
    if (url.rfind("ws://", 0) == 0) start = 5;
    else if (url.rfind("wss://", 0) == 0) { start = 6; e.tls = true; }
    else { fail(); return e; }
    for (unsigned char c : url) if (std::isspace(c) || c < 32 || c == 127) fail();
    if (url.find_first_of("@?#\\") != std::string::npos) fail();
    auto slash = url.find('/', start);
    e.authority = url.substr(start, slash == std::string::npos ? slash : slash-start);
    e.target = slash == std::string::npos ? "/" : url.substr(slash);
    e.port = e.tls ? "443" : "80";
    if (e.authority.empty()) fail();
    if (e.authority.front() == '[') {
        auto end = e.authority.find(']');
        if (end == std::string::npos || end == 1) fail();
        e.host = e.authority.substr(1, end-1);
        Error ec;
        asio::ip::make_address_v6(e.host, ec);
        if (ec) fail();
        if (end+1 < e.authority.size()) {
            if (e.authority[end+1] != ':') fail();
            e.port = e.authority.substr(end+2);
        }
    } else {
        auto colon = e.authority.find(':');
        e.host = e.authority.substr(0, colon);
        if (colon != std::string::npos) e.port = e.authority.substr(colon+1);
        for (unsigned char c : e.host)
            if (!(std::isalnum(c) || c == '-' || c == '.' || c == '_')) fail();
    }
    if (e.host.empty() || e.port.empty() || e.port.size() > 5) fail();
    for (unsigned char c : e.port) if (!std::isdigit(c)) fail();
    int port = std::stoi(e.port);
    if (port < 1 || port > 65535) fail();
    return e;
}

struct Transport::Impl {
    using Plain = websocket::stream<beast::tcp_stream>;
    using Secure = websocket::stream<beast::ssl_stream<beast::tcp_stream>>;
    asio::io_context io;
    ssl::context tls_context{ssl::context::tls_client};
    tcp::resolver resolver{io};
    std::unique_ptr<Plain> plain;
    std::unique_ptr<Secure> secure;
    double timeout;
    explicit Impl(const Endpoint& e, const RunOptions& options) : timeout(options.connect_timeout) {
        if (e.tls) {
            tls_context.set_default_verify_paths();
            if (options.ca_file) tls_context.load_verify_file(*options.ca_file);
            if (!SSL_CTX_set_min_proto_version(tls_context.native_handle(), TLS1_2_VERSION))
                throw std::runtime_error("could not set TLS minimum version");
            secure = std::make_unique<Secure>(io, tls_context);
            secure->next_layer().set_verify_mode(ssl::verify_peer);
            secure->next_layer().set_verify_callback(ssl::host_name_verification(e.host));
            Error ec;
            asio::ip::make_address(e.host, ec);
            if (ec && !SSL_set_tlsext_host_name(secure->next_layer().native_handle(), e.host.c_str()))
                throw std::runtime_error("could not configure TLS server name");
        } else plain = std::make_unique<Plain>(io);
        tcp::resolver::results_type addresses;
        wait(timeout, [&](auto done) {
            resolver.async_resolve(e.host, e.port, [&, done](Error ec, tcp::resolver::results_type result) {
                addresses = std::move(result); done(ec);
            });
        });
        visit([&](auto& ws) {
            wait(timeout, [&](auto done) {
                beast::get_lowest_layer(ws).async_connect(addresses, [done](Error ec, const tcp::endpoint&) { done(ec); });
            });
        });
        if (secure) wait(timeout, [&](auto done) { secure->next_layer().async_handshake(ssl::stream_base::client, done); });
        visit([&](auto& ws) {
            ws.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
            ws.read_message_max(1024 * 1024);
            ws.text(true);
            wait(timeout, [&](auto done) { ws.async_handshake(e.authority, e.target, done); });
        });
    }
    ~Impl() { cancel(); }
    template<class F> void visit(F&& fn) {
        if (secure) fn(*secure); else fn(*plain);
    }
    void cancel() {
        resolver.cancel();
        visit([](auto& ws) {
            Error ignored;
            beast::get_lowest_layer(ws).socket().cancel(ignored);
            beast::get_lowest_layer(ws).socket().close(ignored);
        });
    }
    template<class F> void wait(double seconds, F&& start) {
        io.restart();
        struct Operation {
            asio::steady_timer timer;
            Error result;
            bool expired = false, done = false;
            explicit Operation(asio::io_context& context) : timer(context) {}
        };
        auto operation = std::make_shared<Operation>(io);
        if (seconds > 0) {
            operation->timer.expires_after(std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds)));
            operation->timer.async_wait([this, operation](Error ec) {
                if (!ec && !operation->done) { operation->expired = true; cancel(); }
            });
        }
        start([operation](Error ec) { operation->result = ec; operation->done = true; operation->timer.cancel(); });
        // Beast keeps an idle timer alive between messages. Stop once this operation
        // completes; cancelled timer callbacks retain their own state until drained.
        while (!operation->done) io.run_one();
        if (operation->expired) throw std::runtime_error("connection operation timed out");
        auto result = operation->result;
        if (result == websocket::error::closed || result == asio::error::eof
            || result == asio::error::connection_reset || result == ssl::error::stream_truncated)
            throw ConnectionClosed();
        if (result) throw std::runtime_error("WebSocket operation failed");
    }
};
Transport::Transport(const Endpoint& endpoint, const RunOptions& options)
    : impl_(std::make_unique<Impl>(endpoint, options)) {}
Transport::~Transport() = default;
void Transport::send(const std::string& frame) {
    impl_->visit([&](auto& ws) {
        impl_->wait(impl_->timeout, [&](auto done) { ws.async_write(asio::buffer(frame), [done](Error ec, std::size_t) { done(ec); }); });
    });
}
std::string Transport::receive(bool& text, double timeout) {
    beast::flat_buffer buffer;
    impl_->visit([&](auto& ws) {
        impl_->wait(timeout, [&](auto done) { ws.async_read(buffer, [done](Error ec, std::size_t) { done(ec); }); });
        text = ws.got_text();
    });
    return beast::buffers_to_string(buffer.data());
}
void Transport::close() {
    impl_->visit([&](auto& ws) {
        if (ws.is_open()) impl_->wait(2.0, [&](auto done) { ws.async_close(websocket::close_code::normal, done); });
    });
}
std::optional<int> Transport::close_code() const {
    std::optional<int> code;
    impl_->visit([&](const auto& ws) { if (ws.reason().code != 0) code = ws.reason().code; });
    return code;
}
std::string Transport::close_reason() const {
    std::string reason = "connection lost";
    impl_->visit([&](const auto& ws) { if (ws.reason().code != 0) reason = std::string(ws.reason().reason); });
    return reason;
}
}
