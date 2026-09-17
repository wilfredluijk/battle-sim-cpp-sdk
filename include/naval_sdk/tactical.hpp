#pragma once
#include "bot.hpp"
#include <memory>
#include <utility>

namespace naval_sdk::tactical {
struct Track {
    int track_id{};
    std::string kind = "unknown";
    Point pos{}, observed_pos{};
    Vec2 vel{};
    Tick last_seen_tick{}, first_seen_tick{}, last_active_tick{};
    double confidence{};
    std::string source = "active";
};
struct TrackerOptions {
    double simulation_dt = 0.1, active_gate = 60, passive_bearing_gate_deg = 20, velocity_alpha = 0.3;
    int velocity_window_ticks = 10, staleness_ticks = 40;
};
class Tracker {
public:
    explicit Tracker(const ShipSpecs& specs, int tick_hz = 10, TrackerOptions options = {});
    std::vector<Track> update(const WorldView& view);
    std::vector<Track> tracks() const;
    const Track* get(int track_id) const;
    void reset();
private:
    TrackerOptions options_;
    std::map<int, Track> tracks_;
    std::map<int, std::vector<std::pair<Tick, Point>>> history_;
    int next_id_ = 1;
    Point predict(const Track& track, Tick tick) const;
    void fold_active(int id, const Contact& contact, Tick tick);
    int spawn(const Contact& contact, Tick tick);
};
struct FireSolution { double bearing_deg{}, range{}; Point aim_pos{}; int target_id{}; };
struct EffectiveWeapons { double speed{}, max_range{}, splash{}; int cooldown{}; };
struct GunnerOptions {
    double self_splash_margin = 1.5;
    int max_active_age_ticks = 5;
    bool require_recent_active = true;
    PowerupConfig powerups;
    double simulation_dt = 0.1;
};
class Gunner {
public:
    explicit Gunner(ShipSpecs specs, GunnerOptions options = {});
    EffectiveWeapons effective_weapons(const SelfState& me, const std::optional<std::string>& activation = {}) const;
    void update(const WorldView& view);
    std::optional<FireSolution> solve(const SelfState& me, const Track& track, const WorldView& view,
        const std::optional<std::string>& activation = {}) const;
    bool attempt(Command& command, const SelfState& me, const Track& track, const WorldView& view);
    static FireCommand to_fire_command(const FireSolution& solution);
    void note_fired(Tick tick, std::optional<int> cooldown_ticks = {}, std::optional<int> ammo = {});
    void reset();
    Tick next_fire_tick() const { return next_fire_tick_; }
    bool can_fire(const WorldView& view, const SelfState& me) const;
private:
    ShipSpecs specs_;
    GunnerOptions options_;
    Tick next_fire_tick_ = 0;
    std::optional<std::pair<Tick, int>> pending_;
};
struct HelmOptions {
    double map_width = 700, map_height = 700, wall_margin = 30, turn_aggression_deg = 30;
    double align_threshold_deg = 10, min_turn_throttle = 0.55;
    PowerupConfig powerups;
};
class Helm {
public:
    explicit Helm(ShipSpecs specs, HelmOptions options = {});
    std::pair<double, double> steer_to_bearing(const SelfState& me, double target,
        bool respect_walls = true, double desired_throttle = 1.0) const;
    std::pair<double, double> steer_to_point(const SelfState& me, Point target,
        bool respect_walls = true, double desired_throttle = 1.0) const;
private:
    ShipSpecs specs_;
    HelmOptions options_;
    double wall_override(const SelfState& me, double target) const;
};
enum class EvaderState { IDLE, EVADING, COOLDOWN };
struct EvaderOptions {
    int evasion_ticks = 15, cooldown_ticks = 10;
    double throttle = 1, initial_rudder_sign = 1;
};
class Evader {
public:
    explicit Evader(EvaderOptions options = {});
    virtual ~Evader() = default;
    virtual std::optional<Command> update(const WorldView& view);
    virtual void reset();
    EvaderState state() const { return state_; }
private:
    EvaderOptions options_;
    EvaderState state_ = EvaderState::IDLE;
    Tick state_until_ = 0;
    double rudder_sign_ = 1;
};
class SensorPolicy {
public:
    virtual ~SensorPolicy() = default;
    virtual std::string choose(const WorldView&, const Tracker&) = 0;
    virtual void reset() {}
};
class AlwaysActive : public SensorPolicy {
public: std::string choose(const WorldView&, const Tracker&) override { return "active"; }
};
class AlwaysPassive : public SensorPolicy {
public: std::string choose(const WorldView&, const Tracker&) override { return "passive"; }
};
class DutyCycle : public SensorPolicy {
public:
    int active_ticks, passive_ticks;
    explicit DutyCycle(int active = 10, int passive = 20) : active_ticks(active), passive_ticks(passive) {}
    std::string choose(const WorldView& view, const Tracker&) override;
};
class PingWhenStale : public SensorPolicy {
public:
    int stale_threshold_ticks;
    explicit PingWhenStale(int threshold = 4) : stale_threshold_ticks(threshold) {}
    std::string choose(const WorldView& view, const Tracker& tracker) override;
};
struct ThreatList {
    std::vector<Track> tracks;
    Point me_pos{};
    auto begin() const { return tracks.begin(); }
    auto end() const { return tracks.end(); }
    std::size_t size() const { return tracks.size(); }
    bool empty() const { return tracks.empty(); }
    const Track* nearest() const;
    const Track* farthest() const;
    const Track* by_id(int id) const;
};
struct TacticalContext {
    const WorldView& view;
    const SelfState& me;
    const ShipSpecs& specs;
    Tracker& tracker;
    ThreatList threats;
    double map_width, map_height;
};
enum class IntentKind { ENGAGE, PATROL, RETREAT_TO, HOLD, CUSTOM };
struct Intent {
    IntentKind kind = IntentKind::HOLD;
    std::optional<Track> target;
    std::optional<std::array<double, 4>> rect;
    std::optional<Point> point;
    std::optional<Command> command;
    static Intent engage(const Track& target);
    static Intent patrol(std::array<double, 4> rect);
    static Intent retreat_to(Point point);
    static Intent hold();
    static Intent custom(Command command);
};
class TacticalBot : public Bot {
public:
    std::unique_ptr<Tracker> tracker;
    std::unique_ptr<Gunner> gunner;
    std::unique_ptr<Helm> helm;
    std::unique_ptr<Evader> evader;
    std::unique_ptr<SensorPolicy> sensor_policy = std::make_unique<AlwaysActive>();
    virtual Intent decide(const TacticalContext&) { return Intent::hold(); }
    virtual void on_tactical_welcome(const Welcome&) {}
    void on_welcome(const Welcome& welcome) override;
    void on_game_start(Tick tick, Point position, double heading) override;
    Command on_tick(const WorldView& view) override;
private:
    std::size_t patrol_corner_ = 0;
    Command intent_to_command(const Intent& intent, const TacticalContext& context);
};
}
