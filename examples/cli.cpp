#include <naval_sdk/naval_sdk.hpp>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc == 2 && std::string(argv[1]) == "--version") {
        std::cout << "naval-sdk-cpp " << naval_sdk::version << '\n'; return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "replay") {
        try {
            naval_sdk::Bot bot;
            naval_sdk::replay(bot, argv[2], [](const naval_sdk::ReplayDecision& d) { std::cout << d.command.dump() << '\n'; });
            return 0;
        } catch (const std::exception&) { std::cerr << "Could not replay recording\n"; return 1; }
    }
    std::cout << "naval-sdk --version\nnaval-sdk replay PATH   Replay with a hold-station bot\n"
        "Use my_bot or hunter_bot to connect to a server.\n";
    return argc == 1 || (argc == 2 && std::string(argv[1]) == "--help") ? 0 : 1;
}
