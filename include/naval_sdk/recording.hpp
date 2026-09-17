#pragma once
#include "bot.hpp"
#include <cstdio>
#include <filesystem>

namespace naval_sdk {
class BotRecorder {
public:
    // Creates a new file exclusively; throws if it already exists.
    explicit BotRecorder(const std::filesystem::path& path);
    ~BotRecorder();
    BotRecorder(const BotRecorder&) = delete;
    BotRecorder& operator=(const BotRecorder&) = delete;
    void record(const Json& message);
private:
    std::FILE* file_ = nullptr;
    void write(const Json& value);
};
struct ReplayDecision {
    std::string match_id;
    Tick tick{};
    Json command;
};
// Streaming overload keeps memory bounded. Observations are fixed, not re-simulated.
void replay(Bot& bot, const std::filesystem::path& path,
    const std::function<void(const ReplayDecision&)>& consume);
std::vector<ReplayDecision> replay(Bot& bot, const std::filesystem::path& path);
}
