#include <naval_sdk/naval_sdk.hpp>
int main() {
    naval_sdk::Bot bot;
    naval_sdk::RunOptions options;
    options.url = "invalid";
    // Pull the full transport and its transitive link dependencies into the executable.
    try { naval_sdk::run(bot,options); return 1; }
    catch (const std::invalid_argument&) {}
    return naval_sdk::Command{}.to_dict(1,"consumer")["type"] == "command" ? 0 : 1;
}
