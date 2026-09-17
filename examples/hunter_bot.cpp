#include <naval_sdk/naval_sdk.hpp>
#include <iostream>

class HunterBot : public naval_sdk::tactical::TacticalBot {
public:
    int match_limit = 0, completed = 0;
    naval_sdk::tactical::Intent decide(const naval_sdk::tactical::TacticalContext& ctx) override {
        if (auto target = ctx.threats.nearest()) return naval_sdk::tactical::Intent::engage(*target);
        return naval_sdk::tactical::Intent::patrol({100, 100, ctx.map_width - 100, ctx.map_height - 100});
    }
    bool on_game_over(const naval_sdk::GameOver&) override {
        return ++completed < match_limit || match_limit == 0;
    }
};
int main(int argc, char* argv[]) {
    try {
        auto options = naval_sdk::parse_command_line(argc, argv, "cpp-hunter");
        if (options.help) { std::cout << naval_sdk::connection_help(); return 0; }
        HunterBot bot;
        bot.match_limit = options.matches;
        std::unique_ptr<naval_sdk::BotRecorder> recorder;
        if (options.record) recorder = std::make_unique<naval_sdk::BotRecorder>(*options.record);
        options.connection.recorder = recorder.get();
        naval_sdk::run(bot, options.connection);
        return bot.diagnostics.rejected_commands.empty() ? 0 : 1;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
