#pragma once
#include "helpers.hpp"
#include <nlohmann/json.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace naval_sdk {
using Json = nlohmann::json;
using Tick = std::int64_t;
inline constexpr const char* version = "0.1.0";

struct ShipSpecs {
    double max_forward_speed{}, max_reverse_speed{}, acceleration{}, turn_rate_deg_per_s{};
    int hull_hp{}, max_ammo{}, gun_cooldown_ticks{};
    double hit_radius{}, shell_speed{}, max_shell_range{}, splash_radius{};
    int max_splash_damage{};
    static ShipSpecs from_dict(const Json& data);
    void validate() const;
};
struct MapInfo {
    int width{}, height{};
    static MapInfo from_dict(const Json& data);
};
struct PowerupConfig {
    int overdrive_duration_ticks = 50;
    double overdrive_speed_mult = 1.6, overdrive_accel_mult = 1.6, overdrive_turn_mult = 1.5;
    int reinforced_hull_duration_ticks = 70;
    double reinforced_hull_damage_mult = 0.45;
    int repair_drones_duration_ticks = 50, repair_drones_hp_per_tick = 1, repair_drones_instant_hp = 20;
    int smoke_screen_duration_ticks = 80;
    double smoke_screen_radius = 70.0;
    int rapid_fire_duration_ticks = 50;
    double rapid_fire_cooldown_mult = 0.5;
    int heavy_shell_duration_ticks = 30;
    double heavy_shell_splash_mult = 1.5, heavy_shell_damage_mult = 1.3;
    int long_range_duration_ticks = 40;
    double long_range_range_mult = 1.5, long_range_speed_mult = 1.6;
    int awacs_duration_ticks = 60;
    double awacs_range_mult = 2.0, awacs_silent_jitter = 15.0, awacs_silent_confidence = 0.6;
    int silent_running_duration_ticks = 80;
    double silent_running_active_range_mult = 0.5;
    int counter_battery_arm_ticks = 60, counter_battery_reveal_ticks = 15;
    int emp_burst_duration_ticks = 40;
    double emp_burst_radius = 130.0, emp_gun_cooldown_mult = 2.0;
    int decoy_flare_duration_ticks = 60;
    double decoy_flare_distance_min = 80.0, decoy_flare_distance_max = 140.0;
    Json raw = Json::object();
    static PowerupConfig from_dict(const Json& data);
    void validate() const;
};
struct SensorConfig {
    double active_radar_range = 350.0, active_radar_noise = 2.0;
    double passive_hear_active_range = 500.0, passive_hear_nearby_range = 150.0;
    double passive_bearing_noise_deg = 5.0;
    static SensorConfig from_dict(const Json& data);
};
struct MatchConfiguration {
    std::string protocol_version;
    int revision = 1;
    double simulation_dt = 0.1;
    int tick_hz{}, deadline_ms{};
    MapInfo map;
    ShipSpecs ship_specs;
    SensorConfig sensors;
    PowerupConfig powerups;
    std::vector<std::string> available_powerups;
    int match_timeout_ticks = 3000, wall_bump_damage = 2;
    Json raw = Json::object();
    static MatchConfiguration from_dict(const Json& data);
};
struct Welcome {
    std::string bot_id, ship_id;
    MapInfo map;
    int tick_hz{};
    ShipSpecs ship_specs;
    std::vector<std::string> available_powerups;
    double simulation_dt = 0.1;
    std::string protocol_version = "1.0", config_hash;
    Json configuration = Json::object();
    static Welcome from_dict(const Json& data);
    std::optional<MatchConfiguration> rules() const;
    Welcome with_configuration(const Json& data, const std::string& hash) const;
};
struct PowerupStatus {
    std::string id;
    bool used = false;
    int active_ticks_left = 0;
    static PowerupStatus from_dict(const Json& data);
};
struct GameStart {
    Tick tick{};
    Point starting_position{};
    double starting_heading_deg{};
    std::optional<ShipSpecs> ship_specs;
    double simulation_dt = 0.1;
    std::string match_id;
    static GameStart from_dict(const Json& data);
};
struct SelfState {
    Point pos{};
    double heading_deg{}, speed{};
    int hp{}, ammo{};
    double rudder{}, throttle{};
    std::vector<std::string> selected_powerups;
    std::vector<PowerupStatus> powerup_status;
    std::optional<int> gun_cooldown_ticks_left;
    int emp_ticks_left = 0;
    static SelfState from_dict(const Json& data);
    const PowerupStatus* powerup(const std::string& id) const;
    bool powerup_ready(const std::string& id) const;
    bool powerup_active(const std::string& id) const;
};
struct Contact {
    std::string id, kind = "unknown";
    Point pos{};
    double bearing_deg{};
    std::optional<double> range;
    double confidence{};
    static Contact from_dict(const Json& data);
};
struct HitEvent { int amount{}; };
struct ShellSplashEvent { Point pos{}; };
struct PowerupActivatedEvent {
    bool own = false;
    std::optional<std::string> contact_id;
    std::string powerup;
};
using TickEvent = std::variant<HitEvent, ShellSplashEvent, PowerupActivatedEvent, Json>;
TickEvent parse_event(const Json& data);
struct WorldView {
    Tick tick{};
    int deadline_ms{};
    SelfState self_state;
    std::vector<Contact> contacts;
    std::vector<TickEvent> events;
    std::string match_id;
    static WorldView from_dict(const Json& data);
    const SelfState& me() const { return self_state; }
    const Contact* nearest_contact() const;
};
struct GameOver {
    std::optional<std::string> winner;
    Tick final_tick{};
    std::string replay_id;
    static GameOver from_dict(const Json& data);
};
struct FireCommand {
    double bearing_deg{}, range{};
    Json to_dict() const;
};
struct Command {
    double throttle = 0.0, rudder = 0.0;
    std::string sensor_mode = "active";
    std::optional<FireCommand> fire;
    std::optional<std::string> activate_powerup;
    Command& fire_at(Point target, Point shooter = {0, 0},
        std::optional<Vec2> velocity = std::nullopt, double shell_speed = 70.0,
        std::optional<double> range = std::nullopt, bool lead = true);
    Json to_dict(Tick tick, const std::string& match_id = "") const;
};
}
