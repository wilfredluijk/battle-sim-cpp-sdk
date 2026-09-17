#include <naval_sdk/protocol.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace naval_sdk {
namespace {
void object(const Json& j) {
    if (!j.is_object()) throw std::invalid_argument("expected an object");
}
double finite(double n) {
    if (!std::isfinite(n) || std::abs(n) > std::numeric_limits<float>::max())
        throw std::invalid_argument("number must fit the server's finite f32 range");
    return n;
}
double number(const Json& j) {
    if (!j.is_number()) throw std::invalid_argument("expected a number");
    return finite(j.get<double>());
}
double nonnegative(double n, bool positive = false) {
    finite(n);
    if (n < 0 || (positive && n == 0)) throw std::invalid_argument("invalid configuration number");
    return n;
}
template<class T = int> T integer(const Json& j) {
    if (!j.is_number()) throw std::invalid_argument("expected an integer");
    if (j.is_number_unsigned()) {
        auto n = j.get<std::uint64_t>();
        if (n > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) throw std::invalid_argument("integer out of range");
        return static_cast<T>(n);
    }
    if (j.is_number_integer()) {
        auto n = j.get<std::int64_t>();
        if (n < std::numeric_limits<T>::lowest() || n > std::numeric_limits<T>::max()) throw std::invalid_argument("integer out of range");
        return static_cast<T>(n);
    }
    long double n = static_cast<long double>(j.get<double>());
    if (!std::isfinite(n) || std::floor(n) != n || n < std::numeric_limits<T>::lowest()
        || n >= -static_cast<long double>(std::numeric_limits<T>::lowest())) throw std::invalid_argument("integer out of range");
    return static_cast<T>(n);
}
Point point(const Json& j) {
    if (!j.is_array() || j.size() < 2) throw std::invalid_argument("expected a point");
    return {number(j.at(0)), number(j.at(1))};
}
std::vector<std::string> strings(const Json& j) {
    if (!j.is_array()) throw std::invalid_argument("expected a string array");
    return j.get<std::vector<std::string>>();
}
}

ShipSpecs ShipSpecs::from_dict(const Json& d) {
    object(d);
    ShipSpecs s;
#define NUM(name) s.name = number(d.at(#name));
#define INT(name) s.name = integer(d.at(#name));
    NUM(max_forward_speed) NUM(max_reverse_speed) NUM(acceleration) NUM(turn_rate_deg_per_s)
    INT(hull_hp) INT(max_ammo) INT(gun_cooldown_ticks)
    NUM(hit_radius) NUM(shell_speed) NUM(max_shell_range) NUM(splash_radius) INT(max_splash_damage)
#undef NUM
#undef INT
    s.validate();
    return s;
}
void ShipSpecs::validate() const {
    for (double n : {max_forward_speed, max_reverse_speed, acceleration, turn_rate_deg_per_s,
        double(hull_hp), double(max_ammo), double(gun_cooldown_ticks), hit_radius,
        shell_speed, max_shell_range, splash_radius, double(max_splash_damage)}) nonnegative(n, true);
}
MapInfo MapInfo::from_dict(const Json& d) {
    MapInfo m{integer(d.at("width")), integer(d.at("height"))};
    if (m.width <= 0 || m.height <= 0) throw std::invalid_argument("map dimensions must be positive");
    return m;
}
PowerupConfig PowerupConfig::from_dict(const Json& d) {
    object(d);
    PowerupConfig p;
#define P_NUM(name, positive) if (d.contains(#name)) p.name = number(d.at(#name));
#define P_INT(name, positive) if (d.contains(#name)) p.name = integer(d.at(#name));
#include "powerup_fields.inc"
#undef P_NUM
#undef P_INT
    p.raw = d;
    p.validate();
    return p;
}
void PowerupConfig::validate() const {
#define P_NUM(name, positive) nonnegative(name, positive);
#define P_INT(name, positive) nonnegative(name, positive);
#include "powerup_fields.inc"
#undef P_NUM
#undef P_INT
    if (awacs_silent_confidence > 1 || decoy_flare_distance_min > decoy_flare_distance_max)
        throw std::invalid_argument("invalid confidence or decoy distances");
}
SensorConfig SensorConfig::from_dict(const Json& d) {
    object(d);
    SensorConfig s;
#define SENSOR(name, positive) if (d.contains(#name)) s.name = nonnegative(number(d.at(#name)), positive);
    SENSOR(active_radar_range, true) SENSOR(active_radar_noise, false)
    SENSOR(passive_hear_active_range, true) SENSOR(passive_hear_nearby_range, true)
    SENSOR(passive_bearing_noise_deg, false)
#undef SENSOR
    return s;
}
MatchConfiguration MatchConfiguration::from_dict(const Json& d) {
    object(d);
    const auto& sim = d.at("sim_config");
    object(sim);
    auto caps = d.value("capabilities", Json::object());
    object(caps);
    for (auto key : {"empty_loadout", "own_ship_telemetry"})
        if (caps.contains(key) && !caps.at(key).is_boolean())
            throw std::invalid_argument("capabilities must be boolean");
    MatchConfiguration c;
    c.protocol_version = d.at("protocol_version").get<std::string>();
    c.revision = integer(d.value("revision", Json(1)));
    c.simulation_dt = nonnegative(number(d.at("simulation_dt")), true);
    c.tick_hz = integer(d.at("tick_hz"));
    c.deadline_ms = integer(d.at("deadline_ms"));
    c.map = MapInfo::from_dict(d.at("map"));
    c.ship_specs = ShipSpecs::from_dict(d.at("ship_specs"));
    c.sensors = SensorConfig::from_dict(sim);
    c.powerups = PowerupConfig::from_dict(sim.value("powerups", Json::object()));
    c.available_powerups = strings(d.at("available_powerups"));
    for (const auto& id : c.available_powerups)
        if (id.empty()) throw std::invalid_argument("empty powerup id");
    c.match_timeout_ticks = integer(d.value("match_timeout_ticks", Json(3000)));
    c.wall_bump_damage = integer(sim.value("wall_bump_damage", Json(2)));
    if (c.revision < 0 || c.tick_hz <= 0 || c.deadline_ms <= 0 || c.match_timeout_ticks <= 0 || c.wall_bump_damage < 0)
        throw std::invalid_argument("invalid configuration counts");
    c.raw = d;
    return c;
}
Welcome Welcome::from_dict(const Json& d) {
    Welcome w;
    w.bot_id = d.at("bot_id").get<std::string>();
    w.ship_id = d.at("ship_id").get<std::string>();
    w.map = MapInfo::from_dict(d.at("map"));
    w.tick_hz = integer(d.at("tick_hz"));
    if (w.tick_hz <= 0) throw std::invalid_argument("tick_hz must be positive");
    w.ship_specs = ShipSpecs::from_dict(d.at("ship_specs"));
    w.available_powerups = strings(d.value("available_powerups", Json::array()));
    w.simulation_dt = nonnegative(number(d.value("simulation_dt", Json(0.1))), true);
    w.protocol_version = d.value("protocol_version", std::string("1.0"));
    w.config_hash = d.value("config_hash", std::string());
    w.configuration = d.value("configuration", Json::object());
    object(w.configuration);
    return w;
}
std::optional<MatchConfiguration> Welcome::rules() const {
    if (configuration.empty()) return std::nullopt;
    return MatchConfiguration::from_dict(configuration);
}
Welcome Welcome::with_configuration(const Json& data, const std::string& hash) const {
    if (hash.empty()) throw std::invalid_argument("config_hash must not be empty");
    auto c = MatchConfiguration::from_dict(data);
    auto w = *this;
    w.configuration = data; w.config_hash = hash; w.ship_specs = c.ship_specs;
    w.map = c.map; w.tick_hz = c.tick_hz; w.simulation_dt = c.simulation_dt;
    w.protocol_version = c.protocol_version; w.available_powerups = c.available_powerups;
    return w;
}
PowerupStatus PowerupStatus::from_dict(const Json& d) {
    return {d.at("id").get<std::string>(), d.value("used", false), integer(d.value("active_ticks_left", Json(0)))};
}
GameStart GameStart::from_dict(const Json& d) {
    GameStart s;
    s.tick = integer<Tick>(d.at("tick")); s.starting_position = point(d.at("starting_position"));
    s.starting_heading_deg = number(d.at("starting_heading_deg"));
    if (d.contains("ship_specs")) s.ship_specs = ShipSpecs::from_dict(d.at("ship_specs"));
    s.simulation_dt = nonnegative(number(d.value("simulation_dt", Json(0.1))), true);
    s.match_id = d.value("match_id", std::string());
    return s;
}
SelfState SelfState::from_dict(const Json& d) {
    SelfState s;
    s.pos = point(d.at("pos")); s.heading_deg = number(d.at("heading_deg")); s.speed = number(d.at("speed"));
    s.hp = integer(d.at("hp")); s.ammo = integer(d.at("ammo"));
    s.rudder = number(d.at("rudder")); s.throttle = number(d.at("throttle"));
    s.selected_powerups = strings(d.value("selected_powerups", Json::array()));
    const auto statuses = d.value("powerup_status", Json::array());
    if (!statuses.is_array()) throw std::invalid_argument("powerup_status must be an array");
    for (const auto& status : statuses) s.powerup_status.push_back(PowerupStatus::from_dict(status));
    if (d.contains("gun_cooldown_ticks_left")) s.gun_cooldown_ticks_left = std::max(0, integer(d.at("gun_cooldown_ticks_left")));
    s.emp_ticks_left = std::max(0, integer(d.value("emp_ticks_left", Json(0))));
    return s;
}
const PowerupStatus* SelfState::powerup(const std::string& id) const {
    for (const auto& p : powerup_status) if (p.id == id) return &p;
    return nullptr;
}
bool SelfState::powerup_ready(const std::string& id) const { auto p = powerup(id); return p && !p->used; }
bool SelfState::powerup_active(const std::string& id) const { auto p = powerup(id); return p && p->active_ticks_left > 0; }
Contact Contact::from_dict(const Json& d) {
    Contact c;
    c.id = d.at("id").get<std::string>(); c.kind = d.value("kind", std::string("unknown"));
    c.pos = point(d.at("pos")); c.bearing_deg = number(d.at("bearing_deg"));
    if (d.contains("range") && !d.at("range").is_null()) c.range = number(d.at("range"));
    c.confidence = number(d.value("confidence", Json(0.0)));
    return c;
}
TickEvent parse_event(const Json& d) {
    if (!d.is_object()) return Json{{"type", "unknown"}, {"raw", d}};
    try {
        auto type = d.value("type", std::string());
        if (type == "hit") return HitEvent{integer(d.at("amount"))};
        if (type == "shell_splash") return ShellSplashEvent{point(d.at("pos"))};
        if (type == "powerup_activated") {
            PowerupActivatedEvent e;
            e.own = d.at("own").get<bool>(); e.powerup = d.at("powerup").get<std::string>();
            if (d.contains("contact_id") && !d.at("contact_id").is_null()) e.contact_id = d.at("contact_id").get<std::string>();
            return e;
        }
    } catch (const Json::exception&) { } catch (const std::invalid_argument&) { }
    return d;
}
WorldView WorldView::from_dict(const Json& d) {
    WorldView v;
    v.tick = integer<Tick>(d.at("tick")); v.deadline_ms = integer(d.at("deadline_ms"));
    v.match_id = d.value("match_id", std::string()); v.self_state = SelfState::from_dict(d.at("self"));
    auto contacts = d.value("contacts", Json::array()), events = d.value("events", Json::array());
    if (!contacts.is_array() || !events.is_array()) throw std::invalid_argument("contacts and events must be arrays");
    for (const auto& c : contacts) v.contacts.push_back(Contact::from_dict(c));
    for (const auto& e : events) v.events.push_back(parse_event(e));
    return v;
}
const Contact* WorldView::nearest_contact() const {
    const Contact* nearest = nullptr;
    for (const auto& c : contacts) if (c.range && (!nearest || *c.range < *nearest->range)) nearest = &c;
    return nearest;
}
GameOver GameOver::from_dict(const Json& d) {
    GameOver g;
    if (d.contains("winner") && !d.at("winner").is_null()) g.winner = d.at("winner").get<std::string>();
    g.final_tick = integer<Tick>(d.at("final_tick")); g.replay_id = d.at("replay_id").get<std::string>();
    return g;
}
Json FireCommand::to_dict() const { return {{"bearing_deg", finite(bearing_deg)}, {"range", finite(range)}}; }
Command& Command::fire_at(Point target, Point shooter, std::optional<Vec2> velocity, double speed,
    std::optional<double> range_override, bool lead) {
    auto aim = target;
    if (lead && velocity && *velocity != Vec2{0, 0}) {
        if (auto predicted = lead_target(shooter, target, *velocity, speed)) aim = *predicted;
    }
    fire = FireCommand{bearing_to(shooter, aim), range_override.value_or(distance(shooter, aim))};
    return *this;
}
Json Command::to_dict(Tick tick, const std::string& match_id) const {
    if (sensor_mode != "active" && sensor_mode != "passive") throw std::invalid_argument("invalid sensor mode");
    if (activate_powerup && activate_powerup->empty()) throw std::invalid_argument("empty powerup activation");
    Json out{{"type", "command"}, {"tick", tick}, {"throttle", finite(throttle)},
        {"rudder", finite(rudder)}, {"sensor_mode", sensor_mode}};
    if (!match_id.empty()) out["match_id"] = match_id;
    if (fire) out["fire"] = fire->to_dict();
    if (activate_powerup) out["activate_powerup"] = *activate_powerup;
    return out;
}
}
