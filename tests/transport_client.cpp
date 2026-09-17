#include <naval_sdk/naval_sdk.hpp>
#include <iostream>
#include <cstdlib>

using namespace naval_sdk;
class Observer : public Bot {
public:
    Json starts = Json::array(), hashes = Json::array();
    int rounds = 0;
    bool telemetry = true;
    bool bad_command = false;
    std::vector<std::string> choose_powerups(const Welcome&) override { return {"rapid_fire","heavy_shell"}; }
    void on_welcome(const Welcome& w) override { hashes.push_back(w.config_hash); }
    void on_game_start_event(const GameStart& s) override {
        starts.push_back({{"match_id",s.match_id},{"shell_speed",welcome->ship_specs.shell_speed}});
    }
    Command on_tick(const WorldView& v) override {
        telemetry = telemetry && v.me().gun_cooldown_ticks_left.has_value();
        Command cmd; cmd.throttle = 0.1;
        if (bad_command) cmd.sensor_mode = "invalid";
        return cmd;
    }
    bool on_game_over(const GameOver&) override { return ++rounds < 2; }
};
int main(int argc,char* argv[]) {
    if (argc < 3) return 2;
    Observer bot;
    RunOptions options;
    options.url = argv[1]; options.token = "synthetic-cpp-test-token"; options.name = "cpp-test";
    if (auto name = std::getenv("NAVAL_TEST_BOT_NAME")) options.name = name;
    options.connect_timeout = 0.6; options.reconnect_delay = 0.01;
    std::string mode = argv[2];
    if (mode == "retry" || mode == "active-close" || mode == "auth") options.reconnect_attempts = 2;
    if (mode == "invalid") bot.bad_command = true;
    if (argc > 4) options.ca_file = argv[4];
    Json result;
    try {
        std::unique_ptr<BotRecorder> recorder;
        if (argc > 3 && std::string(argv[3]) != "-") recorder = std::make_unique<BotRecorder>(argv[3]);
        options.recorder = recorder.get();
        auto outcome = run_async(bot, options).get();
        result["result"] = outcome ? Json(outcome->replay_id) : Json(nullptr);
        result["exception"] = false;
    } catch (const ProtocolMismatch&) { result["exception"] = "protocol"; }
      catch (const std::exception&) { result["exception"] = true; }
    result["rounds"] = bot.rounds; result["ticks"] = bot.diagnostics.ticks;
    result["malformed"] = bot.diagnostics.malformed_frames; result["callback_errors"] = bot.diagnostics.callback_errors;
    result["rejected"] = bot.diagnostics.rejected_commands; result["starts"] = bot.starts;
    result["hashes"] = bot.hashes; result["telemetry"] = bot.telemetry;
    result["running"] = bot.running(); result["phase"] = bot.phase;
    if (bot.diagnostics.last_disconnect) {
        auto& d = *bot.diagnostics.last_disconnect;
        result["disconnect_phase"] = d.phase; result["disconnect_code"] = d.code ? Json(*d.code) : Json(nullptr);
    }
    if (argc > 3 && std::string(argv[3]) != "-") {
        try { Bot replay_bot; result["replayed"] = replay(replay_bot,argv[3]).size(); }
        catch (...) { result["replayed"] = -1; }
    }
    std::cout << result.dump() << '\n';
}
