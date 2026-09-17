#include <naval_sdk/naval_sdk.hpp>
#include <iostream>

class MyBot : public naval_sdk::Bot {
public:
    int match_limit = 0, completed = 0;
    naval_sdk::Command on_tick(const naval_sdk::WorldView&) override {
        naval_sdk::Command command;
        command.throttle = 0.6;
        command.rudder = 0.2;
        return command;
    }
    bool on_game_over(const naval_sdk::GameOver&) override {
        return ++completed < match_limit || match_limit == 0;
    }
};
int main(int argc, char* argv[]) {
    try {
        auto options = naval_sdk::parse_command_line(argc, argv, "cpp-my-bot");
        if (options.help) { std::cout << naval_sdk::connection_help(); return 0; }
        MyBot bot;
        bot.match_limit = options.matches;
        std::unique_ptr<naval_sdk::BotRecorder> recorder;
        if (options.record) recorder = std::make_unique<naval_sdk::BotRecorder>(*options.record);
        options.connection.recorder = recorder.get();
        naval_sdk::run(bot, options.connection);
        return bot.diagnostics.rejected_commands.empty() ? 0 : 1;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
