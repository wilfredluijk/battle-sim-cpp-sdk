#include <naval_sdk/recording.hpp>
#include "runtime_guard.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>

namespace naval_sdk {
namespace {
Json redact(const Json& value) {
    if (value.is_object()) {
        Json out = Json::object();
        for (auto it = value.begin(); it != value.end(); ++it) {
            auto key = it.key();
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            bool secret = false;
            for (auto word : {"token", "password", "credential", "authorization"})
                secret = secret || key.find(word) != std::string::npos;
            out[it.key()] = secret ? Json("[redacted]") : redact(it.value());
        }
        return out;
    }
    if (value.is_array()) {
        Json out = Json::array();
        for (const auto& item : value) out.push_back(redact(item));
        return out;
    }
    if (value.is_number_float() && !std::isfinite(value.get<double>()))
        throw std::invalid_argument("recordings require finite JSON numbers");
    return value;
}
const Json header{{"format", "naval-sdk-bot-views"}, {"version", 1}};
}
BotRecorder::BotRecorder(const std::filesystem::path& path) {
#ifdef _WIN32
    file_ = _wfopen(path.c_str(), L"wbx");
#else
    file_ = std::fopen(path.c_str(), "wbx");
#endif
    if (!file_) throw std::runtime_error("could not create recording; it may already exist");
    try { write(header); } catch (...) { std::fclose(file_); file_ = nullptr; throw; }
}
BotRecorder::~BotRecorder() { if (file_) std::fclose(file_); }
void BotRecorder::write(const Json& value) {
    auto line = value.dump() + '\n';
    if (std::fwrite(line.data(), 1, line.size(), file_) != line.size() || std::fflush(file_) != 0)
        throw std::runtime_error("could not write recording");
}
void BotRecorder::record(const Json& message) {
    static const std::set<std::string> types{"welcome", "configuration", "game_start", "tick", "game_over", "lobby", "error"};
    if (types.count(message.value("type", std::string()))) write(redact(message));
}
void replay(Bot& bot, const std::filesystem::path& path, const std::function<void(const ReplayDecision&)>& consume) {
    RuntimeGuard guard(bot);
    std::ifstream input(path);
    std::string line;
    if (!input || !std::getline(input, line) || Json::parse(line) != header)
        throw std::invalid_argument("unsupported bot-view recording");
    bot.welcome.reset(); bot.match_id.clear(); bot.last_tick = 0; bot.diagnostics = {};
    Session session(bot);
    while (std::getline(input, line)) {
        auto msg = Json::parse(line);
        if (!msg.is_object()) throw std::invalid_argument("recording frames must be objects");
        for (const auto& command : session.handle(msg))
            if (command.at("type") == "command")
                consume({command.at("match_id").get<std::string>(), command.at("tick").get<Tick>(), command});
        if (session.stop || session.fatal) break;
    }
    if (input.bad()) throw std::runtime_error("could not read recording");
}
std::vector<ReplayDecision> replay(Bot& bot, const std::filesystem::path& path) {
    std::vector<ReplayDecision> decisions;
    replay(bot, path, [&](const ReplayDecision& d) { decisions.push_back(d); });
    return decisions;
}
}
