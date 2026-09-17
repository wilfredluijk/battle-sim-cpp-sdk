#pragma once
#include "protocol.hpp"
#include <atomic>
#include <functional>
#include <future>
#include <map>
#include <stdexcept>

namespace naval_sdk {
struct TickTiming {
    std::string match_id;
    Tick tick{};
    double elapsed_ms{};
    int deadline_ms{};
    bool over_budget = false;
};
struct DisconnectInfo {
    std::optional<int> code;
    std::string reason, phase;
};
struct RuntimeDiagnostics {
    std::uint64_t ticks = 0, overruns = 0, callback_errors = 0, malformed_frames = 0;
    std::map<std::string, std::uint64_t> rejected_commands;
    std::optional<TickTiming> last_timing;
    std::optional<DisconnectInfo> last_disconnect;
};
class ProtocolMismatch : public std::invalid_argument {
public:
    ProtocolMismatch() : std::invalid_argument("this SDK supports server protocol 3.x") {}
};
void check_protocol(const std::string& version);
class BotRecorder;
class Bot;
struct RunOptions {
    std::string host = "localhost", path = "/bot", name = "bot";
    int port = 7878;
    std::string version = "naval-sdk-cpp/0.1.0";
    std::optional<std::string> url, token;
    BotRecorder* recorder = nullptr;
    int reconnect_attempts = 0;
    double reconnect_delay = 1.0;
    // Bounds DNS/TCP/TLS/WebSocket setup, first welcome, and writes.
    double connect_timeout = 10.0;
    std::optional<std::string> ca_file;
};
std::optional<GameOver> run(Bot& bot, const RunOptions& options = {});
// Bot and recorder must outlive the returned future. Callbacks run on its worker.
std::future<std::optional<GameOver>> run_async(Bot& bot, RunOptions options = {});

class Bot {
public:
    virtual ~Bot() = default;
    std::optional<Welcome> welcome;
    Tick last_tick = 0;
    std::string match_id, phase = "disconnected";
    RuntimeDiagnostics diagnostics;
    virtual bool accept_configuration(const Json&, const std::string&) { return true; }
    virtual void on_welcome(const Welcome&) {}
    virtual std::vector<std::string> choose_powerups(const Welcome&) { return {}; }
    virtual void on_game_start(Tick, Point, double) {}
    virtual void on_game_start_event(const GameStart& start) {
        on_game_start(start.tick, start.starting_position, start.starting_heading_deg);
    }
    virtual Command on_tick(const WorldView&) { return {}; }
    virtual bool on_game_over(const GameOver&) { return true; }
    virtual void on_lobby(Tick) {}
    virtual void on_error(const std::string&, const std::string&) {}
    virtual void on_tick_timing(const TickTiming&) {}
    virtual void on_disconnect(const DisconnectInfo&) {}
    // Call only from a callback on the runtime thread. Receive ownership stays with run().
    void raw_send(const Json& payload);
    Json raw_recv();
    bool running() const { return running_.load(); }
private:
    std::atomic<bool> running_{false};
    std::function<void(const Json&)> send_;
    friend std::optional<GameOver> run(Bot&, const RunOptions&);
    friend class RuntimeGuard;
};

// Synchronous lifecycle dispatcher shared by transport and replay; useful for tests.
class Session {
public:
    explicit Session(Bot& bot) : bot_(bot) {}
    std::vector<Json> ready();
    std::vector<Json> handle(const Json& message);
    Json command(const WorldView& view);
    bool ready_sent = false, stop = false, fatal = false;
    std::optional<GameOver> result;
private:
    Bot& bot_;
    std::optional<std::vector<std::string>> loadout_;
};
}
