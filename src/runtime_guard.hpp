#pragma once
#include <naval_sdk/bot.hpp>

namespace naval_sdk {
class RuntimeGuard {
public:
    explicit RuntimeGuard(Bot& bot) : bot_(bot) {
        if (bot_.running_.exchange(true)) throw std::logic_error("this bot already has an active runtime");
    }
    ~RuntimeGuard() { bot_.send_ = {}; bot_.phase = "disconnected"; bot_.running_ = false; }
private:
    Bot& bot_;
};
}
