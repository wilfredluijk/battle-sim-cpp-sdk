#include <naval_sdk/bot.hpp>
#include <naval_sdk/recording.hpp>
#include "transport.hpp"
#include "runtime_guard.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <set>
#include <thread>
#include <tuple>

namespace naval_sdk {
namespace {
template<class F> bool notify(Bot& bot, F&& fn) {
    try { fn(); return true; } catch (...) { ++bot.diagnostics.callback_errors; return false; }
}
std::string environment(const char* key) { auto p = std::getenv(key); return p ? p : ""; }
void validate_json(const Json& value) {
    if (value.is_number_float() && !std::isfinite(value.get<double>()))
        throw std::invalid_argument("raw frames require finite JSON numbers");
    if (value.is_object() || value.is_array()) for (const auto& item : value) validate_json(item);
}
bool same_specs(const ShipSpecs& a, const ShipSpecs& b) {
    auto values = [](const ShipSpecs& s) {
        return std::make_tuple(s.max_forward_speed, s.max_reverse_speed, s.acceleration, s.turn_rate_deg_per_s,
            s.hull_hp, s.max_ammo, s.gun_cooldown_ticks, s.hit_radius, s.shell_speed, s.max_shell_range,
            s.splash_radius, s.max_splash_damage);
    };
    return values(a) == values(b);
}
}
void check_protocol(const std::string& version) {
    auto dot = version.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == version.size()) throw ProtocolMismatch();
    for (std::size_t i = 0; i < version.size(); ++i)
        if (i != dot && (version[i] < '0' || version[i] > '9')) throw ProtocolMismatch();
    auto major = version.substr(0, dot);
    auto first = major.find_first_not_of('0');
    if (first == std::string::npos || major.substr(first) != "3") throw ProtocolMismatch();
}
void Bot::raw_send(const Json& payload) {
    if (!send_) throw std::logic_error("raw_send called before connection is open");
    validate_json(payload);
    send_(payload);
}
Json Bot::raw_recv() {
    throw std::logic_error("the runtime owns the receive loop; use typed callbacks or a recorder");
}
std::vector<Json> Session::ready() {
    auto& bot = bot_;
    ready_sent = false;
    if (!bot.welcome) return {};
    const auto& w = *bot.welcome;
    bool accepted = false;
    if (!notify(bot, [&] { accepted = bot.accept_configuration(w.configuration, w.config_hash); }) || !accepted) return {};
    std::vector<std::string> picks;
    if (!notify(bot, [&] { picks = bot.choose_powerups(w); })) return {};
    if (picks.size() != 0 && picks.size() != 2) return {};
    if (std::set<std::string>(picks.begin(), picks.end()).size() != picks.size()) return {};
    for (const auto& p : picks)
        if (std::find(w.available_powerups.begin(), w.available_powerups.end(), p) == w.available_powerups.end()) return {};
    const bool had_loadout = loadout_ && !loadout_->empty();
    if (picks.empty() && had_loadout && !w.configuration.value("capabilities", Json::object()).value("empty_loadout", false)) return {};
    std::vector<Json> out;
    if (!picks.empty() || had_loadout) out.push_back({{"type", "select_powerups"}, {"powerups", picks}});
    loadout_ = picks; ready_sent = true;
    out.push_back({{"type", "ready"}, {"config_hash", w.config_hash}});
    return out;
}
std::vector<Json> Session::handle(const Json& msg) {
    auto& bot = bot_;
    try {
        if (!msg.is_object()) throw std::invalid_argument("expected object");
        auto kind = msg.value("type", std::string());
        if (kind == "welcome") {
            auto w = Welcome::from_dict(msg);
            check_protocol(w.protocol_version);
            w = w.with_configuration(msg.at("configuration"), msg.at("config_hash").get<std::string>());
            check_protocol(w.protocol_version);
            bot.welcome = w; bot.phase = "lobby";
            notify(bot, [&] { bot.on_welcome(w); });
            return ready_sent ? std::vector<Json>{} : ready();
        }
        if (kind == "configuration") {
            if (!bot.welcome) return {};
            auto w = bot.welcome->with_configuration(msg.at("configuration"), msg.at("config_hash").get<std::string>());
            check_protocol(w.protocol_version);
            bot.welcome = w; ready_sent = false;
            notify(bot, [&] { bot.on_welcome(w); });
            return ready();
        }
        if (kind == "game_start") {
            auto s = GameStart::from_dict(msg);
            if (s.match_id.empty() || !bot.welcome) throw std::invalid_argument("game_start requires welcome and match_id");
            if (s.ship_specs && (!same_specs(*s.ship_specs, bot.welcome->ship_specs)
                || s.simulation_dt != bot.welcome->simulation_dt)) {
                // A start may carry changed per-match specs even without a new welcome.
                bot.welcome->ship_specs = *s.ship_specs;
                bot.welcome->simulation_dt = s.simulation_dt;
                auto w = *bot.welcome;
                notify(bot, [&] { bot.on_welcome(w); });
            }
            bot.phase = "running"; bot.match_id = s.match_id; bot.last_tick = s.tick;
            notify(bot, [&] { bot.on_game_start_event(s); });
        } else if (kind == "tick") {
            auto view = WorldView::from_dict(msg);
            if (view.match_id.empty() || !bot.welcome) throw std::invalid_argument("tick requires welcome and match_id");
            bot.last_tick = view.tick; bot.match_id = view.match_id; bot.phase = "running";
            return {command(view)};
        } else if (kind == "game_over") {
            result = GameOver::from_dict(msg); bot.phase = "ended";
            bool keep_running = true;
            notify(bot, [&] { keep_running = bot.on_game_over(*result); });
            stop = !keep_running; ready_sent = false;
        } else if (kind == "lobby") {
            auto tick_value = msg.value("tick", Json(0));
            if (!tick_value.is_number_integer() || (tick_value.is_number_unsigned()
                && tick_value.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<Tick>::max())))
                throw std::invalid_argument("invalid lobby tick");
            Tick tick = tick_value.get<Tick>();
            bot.phase = "lobby"; bot.last_tick = tick; bot.match_id.clear();
            notify(bot, [&] { bot.on_lobby(tick); });
            if (!ready_sent) { loadout_.reset(); return ready(); }
        } else if (kind == "error") {
            auto code = msg.value("code", std::string("unknown"));
            auto message = msg.value("message", std::string());
            ++bot.diagnostics.rejected_commands[code];
            fatal = fatal || code == "unauthorized" || code == "invalid_name" || code == "duplicate_name" || code == "rate_limited";
            notify(bot, [&] { bot.on_error(code, message); });
        }
    } catch (const ProtocolMismatch&) { fatal = true; throw; }
      catch (const Json::exception&) { ++bot.diagnostics.malformed_frames; }
      catch (const std::invalid_argument&) { ++bot.diagnostics.malformed_frames; }
      catch (const std::out_of_range&) { ++bot.diagnostics.malformed_frames; }
    return {};
}
Json Session::command(const WorldView& view) {
    auto& bot = bot_;
    auto start = std::chrono::steady_clock::now();
    Command cmd;
    if (!notify(bot, [&] { cmd = bot.on_tick(view); })) cmd = Command{};
    Json payload;
    try {
        payload = cmd.to_dict(view.tick, view.match_id);
        if (cmd.activate_powerup && bot.welcome) {
            const auto& catalog = bot.welcome->available_powerups;
            if (std::find(catalog.begin(), catalog.end(), *cmd.activate_powerup) == catalog.end())
                throw std::invalid_argument("unknown powerup activation");
        }
        (void)payload.dump();
    } catch (const std::exception&) {
        ++bot.diagnostics.callback_errors;
        payload = Command{}.to_dict(view.tick, view.match_id);
    }
    double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    TickTiming timing{view.match_id, view.tick, elapsed, view.deadline_ms, elapsed > view.deadline_ms};
    ++bot.diagnostics.ticks; bot.diagnostics.last_timing = timing;
    if (timing.over_budget) ++bot.diagnostics.overruns;
    notify(bot, [&] { bot.on_tick_timing(timing); });
    return payload;
}
std::optional<GameOver> run(Bot& bot, const RunOptions& options) {
    if (options.reconnect_attempts < 0 || !std::isfinite(options.reconnect_delay) || options.reconnect_delay < 0
        || options.reconnect_delay > 60 || !std::isfinite(options.connect_timeout) || options.connect_timeout <= 0
        || options.connect_timeout > 300) throw std::invalid_argument("invalid retry or timeout settings");
    std::string host = options.host;
    if (host.find(':') != std::string::npos && !host.empty() && host.front() != '[') host = '[' + host + ']';
    auto ambient_url = environment("BATTLE_SERVER_URL");
    auto uri = options.url.value_or(ambient_url.empty() ? "ws://" + host + ':' + std::to_string(options.port) + options.path : ambient_url);
    auto endpoint = detail::parse_url(uri);
    auto token = options.token.value_or(environment("BATTLE_BOT_TOKEN"));
    RuntimeGuard guard(bot);
    bot.diagnostics = {};
    std::optional<GameOver> last_result;
    for (int attempt = 0;; ++attempt) {
        bot.phase = "connecting"; bot.welcome.reset(); bot.match_id.clear(); bot.last_tick = 0;
        Session session(bot);
        DisconnectInfo info{std::nullopt, "client stopped", "connecting"};
        std::unique_ptr<detail::Transport> transport;
        std::exception_ptr failure;
        try {
            transport = std::make_unique<detail::Transport>(endpoint, options);
            bot.send_ = [&](const Json& frame) { transport->send(frame.dump()); };
            transport->send(Json{{"type", "hello"}, {"name", options.name}, {"version", options.version}, {"token", token}}.dump());
            auto welcome_deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(options.connect_timeout);
            while (!session.stop && !session.fatal) {
                bool is_text = false;
                double timeout = 0;
                if (bot.phase == "connecting") {
                    timeout = std::chrono::duration<double>(welcome_deadline - std::chrono::steady_clock::now()).count();
                    if (timeout <= 0) throw std::runtime_error("welcome timed out");
                }
                auto frame = transport->receive(is_text, timeout);
                if (!is_text) { ++bot.diagnostics.malformed_frames; continue; }
                Json msg = Json::parse(frame, nullptr, false);
                if (msg.is_discarded() || !msg.is_object()) { ++bot.diagnostics.malformed_frames; continue; }
                if (options.recorder) {
                    try { options.recorder->record(msg); }
                    catch (const Json::exception&) { ++bot.diagnostics.malformed_frames; continue; }
                    catch (const std::invalid_argument&) { ++bot.diagnostics.malformed_frames; continue; }
                }
                for (const auto& response : session.handle(msg)) transport->send(response.dump());
            }
            transport->close();
        } catch (const detail::ConnectionClosed&) {
            if (transport) { info.code = transport->close_code(); info.reason = transport->close_reason(); }
        } catch (const ProtocolMismatch&) {
            info.reason = "unsupported protocol"; failure = std::current_exception(); session.fatal = true;
        } catch (...) {
            // Never copy transport exception text: it may contain a URL or peer-controlled data.
            info.reason = "connection failed or operation timed out"; failure = std::current_exception();
        }
        bot.send_ = {};
        if (session.result) last_result = session.result;
        info.phase = bot.phase; bot.diagnostics.last_disconnect = info;
        notify(bot, [&] { bot.on_disconnect(info); });
        bool done = session.stop || session.fatal || bot.phase == "running" || attempt >= options.reconnect_attempts;
        if (done) {
            if (failure) std::rethrow_exception(failure);
            return last_result;
        }
        std::this_thread::sleep_for(std::chrono::duration<double>(options.reconnect_delay));
    }
}
std::future<std::optional<GameOver>> run_async(Bot& bot, RunOptions options) {
    return std::async(std::launch::async, [&bot, options = std::move(options)] { return run(bot, options); });
}
}
