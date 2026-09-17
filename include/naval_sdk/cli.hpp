#pragma once
#include "bot.hpp"
#include <filesystem>

namespace naval_sdk {
struct ConnectionArguments {
    std::optional<std::string> url, host;
    std::optional<int> port;
    std::optional<std::filesystem::path> env_file;
};
RunOptions connection_options(const ConnectionArguments& args,
    const std::string& default_url = "ws://localhost:7878/bot");
struct CliOptions {
    RunOptions connection;
    std::optional<std::filesystem::path> record;
    int matches = 0; // Zero keeps participating until disconnected.
    bool help = false;
};
CliOptions parse_command_line(int argc, char* argv[], const std::string& default_name = "cpp-bot");
std::string connection_help();
}
