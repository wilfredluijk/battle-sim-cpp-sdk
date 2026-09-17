#include <naval_sdk/tactical.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace naval_sdk::tactical {
namespace {
void positive(double x) { if (!std::isfinite(x) || x <= 0) throw std::invalid_argument("expected a finite positive value"); }
double radians(double degrees) { return degrees * std::acos(-1) / 180; }
double elapsed(Tick now, Tick before) { return double(now) - double(before); }
Tick add_ticks(Tick tick, int count) {
    if (count < 0 || tick > std::numeric_limits<Tick>::max() - count) throw std::invalid_argument("invalid tick count");
    return tick + count;
}
}
Tracker::Tracker(const ShipSpecs& specs, int, TrackerOptions options) : options_(options) {
    specs.validate(); positive(options_.simulation_dt); positive(options_.active_gate); positive(options_.passive_bearing_gate_deg);
    if (!std::isfinite(options_.velocity_alpha) || options_.velocity_alpha < 0 || options_.velocity_alpha > 1)
        throw std::invalid_argument("velocity_alpha must be between zero and one");
    options_.velocity_window_ticks = std::max(2, options_.velocity_window_ticks);
}
Point Tracker::predict(const Track& track, Tick tick) const {
    double dt = std::max(0.0, elapsed(tick, track.last_active_tick)) * options_.simulation_dt;
    return {track.observed_pos[0] + track.vel[0]*dt, track.observed_pos[1] + track.vel[1]*dt};
}
void Tracker::fold_active(int id, const Contact& c, Tick tick) {
    auto& t = tracks_.at(id);
    auto& h = history_.at(id);
    h.push_back({tick, c.pos});
    if (h.size() > static_cast<std::size_t>(options_.velocity_window_ticks)) h.erase(h.begin());
    if (h.size() >= 2) {
        double dt = elapsed(tick, h.front().first) * options_.simulation_dt;
        if (dt > 0) {
            Vec2 v{(c.pos[0]-h.front().second[0])/dt, (c.pos[1]-h.front().second[1])/dt};
            if (t.vel == Vec2{0,0}) t.vel = v;
            else for (int i = 0; i < 2; ++i) t.vel[i] = options_.velocity_alpha*v[i] + (1-options_.velocity_alpha)*t.vel[i];
        }
    }
    t.observed_pos = c.pos; t.pos = c.pos; t.last_seen_tick = tick; t.last_active_tick = tick;
    t.confidence = c.confidence; t.source = "active";
    if (t.kind == "unknown") t.kind = c.kind;
}
int Tracker::spawn(const Contact& c, Tick tick) {
    if (next_id_ == std::numeric_limits<int>::max()) throw std::overflow_error("track IDs exhausted");
    int id = next_id_++;
    tracks_[id] = Track{id, c.kind, c.pos, c.pos, {0,0}, tick, tick, tick, c.confidence, "active"};
    history_[id] = {{tick, c.pos}};
    return id;
}
std::vector<Track> Tracker::update(const WorldView& view) {
    std::set<int> matched;
    for (const auto& c : view.contacts) {
        if (!c.range) continue;
        int best = 0; double best_distance = options_.active_gate;
        for (const auto& [id,t] : tracks_) {
            if (matched.count(id)) continue;
            double d = distance(predict(t, view.tick), c.pos);
            if (d < best_distance) { best_distance = d; best = id; }
        }
        if (best) fold_active(best, c, view.tick); else best = spawn(c, view.tick);
        matched.insert(best);
    }
    for (const auto& c : view.contacts) {
        if (c.range) continue;
        int best = 0; double best_bearing = options_.passive_bearing_gate_deg;
        for (const auto& [id,t] : tracks_) {
            if (matched.count(id)) continue;
            double delta = std::abs(signed_bearing_delta(c.bearing_deg, bearing_to(view.me().pos, predict(t, view.tick))));
            if (delta < best_bearing) { best_bearing = delta; best = id; }
        }
        if (best) {
            auto& t = tracks_.at(best);
            t.last_seen_tick = view.tick; t.confidence = c.confidence; t.source = "passive";
            matched.insert(best);
        }
    }
    for (auto it = tracks_.begin(); it != tracks_.end();) {
        auto& t = it->second;
        if (t.last_active_tick != view.tick) {
            t.pos = predict(t, view.tick);
            if (t.last_seen_tick != view.tick) t.source = "dead_reckoned";
        }
        double age = elapsed(view.tick, t.last_seen_tick);
        if (age < 0 || age > options_.staleness_ticks) { history_.erase(it->first); it = tracks_.erase(it); }
        else ++it;
    }
    return tracks();
}
std::vector<Track> Tracker::tracks() const {
    std::vector<Track> out;
    for (const auto& [id,t] : tracks_) { (void)id; out.push_back(t); }
    return out;
}
const Track* Tracker::get(int id) const { auto it = tracks_.find(id); return it == tracks_.end() ? nullptr : &it->second; }
void Tracker::reset() { tracks_.clear(); history_.clear(); next_id_ = 1; }

Gunner::Gunner(ShipSpecs specs, GunnerOptions options) : specs_(specs), options_(options) {
    specs_.validate(); options_.powerups.validate(); positive(options_.simulation_dt);
    if (!std::isfinite(options_.self_splash_margin) || options_.self_splash_margin < 1)
        throw std::invalid_argument("self_splash_margin must be finite and at least one");
}
EffectiveWeapons Gunner::effective_weapons(const SelfState& me, const std::optional<std::string>& activation) const {
    auto active = [&](const char* id) { return me.powerup_active(id) || (activation && *activation == id && me.powerup_ready(id)); };
    const auto& p = options_.powerups;
    double cooldown = specs_.gun_cooldown_ticks * (active("rapid_fire") ? p.rapid_fire_cooldown_mult : 1);
    if (me.emp_ticks_left > 0) cooldown *= p.emp_gun_cooldown_mult;
    if (!std::isfinite(cooldown) || cooldown > std::numeric_limits<int>::max()-1)
        throw std::invalid_argument("effective cooldown out of range");
    return {specs_.shell_speed * (active("long_range_salvo") ? p.long_range_speed_mult : 1),
        specs_.max_shell_range * (active("long_range_salvo") ? p.long_range_range_mult : 1),
        specs_.splash_radius * (active("heavy_shell") ? p.heavy_shell_splash_mult : 1),
        std::max(1, static_cast<int>(std::floor(cooldown + 0.5)))};
}
void Gunner::update(const WorldView& view) {
    if (view.me().gun_cooldown_ticks_left) {
        next_fire_tick_ = add_ticks(view.tick, *view.me().gun_cooldown_ticks_left); pending_.reset();
    } else if (pending_ && view.tick > pending_->first) {
        if (view.me().ammo >= pending_->second) next_fire_tick_ = view.tick;
        pending_.reset();
    }
}
bool Gunner::can_fire(const WorldView& view, const SelfState& me) const {
    return (me.gun_cooldown_ticks_left ? *me.gun_cooldown_ticks_left == 0 : view.tick >= next_fire_tick_) && me.ammo > 0;
}
std::optional<FireSolution> Gunner::solve(const SelfState& me, const Track& track, const WorldView& view,
    const std::optional<std::string>& activation) const {
    if (!can_fire(view, me) || (options_.require_recent_active && elapsed(view.tick, track.last_active_tick) > options_.max_active_age_ticks)) return {};
    auto w = effective_weapons(me, activation);
    auto aim = lead_target(me.pos, track.pos, track.vel, w.speed);
    if (!aim) return {};
    double range = distance(*aim, me.pos);
    if (range > w.max_range) return {};
    double flight = std::max(1.0, std::ceil(range / (w.speed * options_.simulation_dt))) * options_.simulation_dt;
    double angle = radians(me.heading_deg);
    Point future{me.pos[0] + std::sin(angle)*me.speed*flight, me.pos[1] - std::cos(angle)*me.speed*flight};
    double safe = specs_.hit_radius + w.splash * options_.self_splash_margin;
    if (std::min(range, distance(future, *aim)) < safe) return {};
    return FireSolution{bearing_to(me.pos, *aim), range, *aim, track.track_id};
}
bool Gunner::attempt(Command& cmd, const SelfState& me, const Track& track, const WorldView& view) {
    update(view);
    auto solution = solve(me, track, view, cmd.activate_powerup);
    if (!solution) return false;
    cmd.fire = to_fire_command(*solution);
    note_fired(view.tick, effective_weapons(me, cmd.activate_powerup).cooldown, me.ammo);
    return true;
}
FireCommand Gunner::to_fire_command(const FireSolution& s) { return {s.bearing_deg, s.range}; }
void Gunner::note_fired(Tick tick, std::optional<int> cooldown, std::optional<int> ammo) {
    next_fire_tick_ = add_ticks(tick, cooldown.value_or(specs_.gun_cooldown_ticks));
    pending_.reset(); if (ammo) pending_ = std::make_pair(tick, *ammo);
}
void Gunner::reset() { next_fire_tick_ = 0; pending_.reset(); }

Helm::Helm(ShipSpecs specs, HelmOptions options) : specs_(specs), options_(options) {
    specs_.validate(); options_.powerups.validate(); positive(options_.map_width); positive(options_.map_height); positive(options_.turn_aggression_deg);
}
std::pair<double,double> Helm::steer_to_bearing(const SelfState& me, double target, bool walls, double desired) const {
    double bearing = walls ? wall_override(me, target) : target;
    double delta = signed_bearing_delta(bearing, me.heading_deg);
    double rudder = clamp(delta / options_.turn_aggression_deg, -1, 1), throttle = desired;
    if (std::abs(delta) > options_.align_threshold_deg) {
        double scale = clamp((180 - std::abs(delta)) / std::max(180-options_.align_threshold_deg, 1e-6), 0, 1);
        throttle = options_.min_turn_throttle + (desired-options_.min_turn_throttle)*scale;
    }
    return {throttle, rudder};
}
std::pair<double,double> Helm::steer_to_point(const SelfState& me, Point target, bool walls, double desired) const {
    return steer_to_bearing(me, bearing_to(me.pos, target), walls, desired);
}
double Helm::wall_override(const SelfState& me, double target) const {
    const auto& p = options_.powerups;
    bool overdrive = me.powerup_active("overdrive");
    double accel = specs_.acceleration * (overdrive ? p.overdrive_accel_mult : 1);
    double turn = specs_.turn_rate_deg_per_s * (overdrive ? p.overdrive_turn_mult : 1);
    double max_speed = specs_.max_forward_speed * (overdrive ? p.overdrive_speed_mult : 1);
    double yaw = turn * std::abs(me.speed) / max_speed;
    double horizon = std::max(std::abs(me.speed)/accel, std::min(90/std::max(yaw, 1e-6), 3.0));
    double x = me.pos[0], y = me.pos[1], angle = radians(me.heading_deg);
    double fx = x + std::sin(angle)*me.speed*horizon, fy = y - std::cos(angle)*me.speed*horizon;
    Point push{0,0};
    if (std::min(x,fx) < options_.wall_margin) push[0] = 1;
    else if (std::max(x,fx) > options_.map_width-options_.wall_margin) push[0] = -1;
    if (std::min(y,fy) < options_.wall_margin) push[1] = 1;
    else if (std::max(y,fy) > options_.map_height-options_.wall_margin) push[1] = -1;
    if (push == Point{0,0}) return target;
    double bearing = bearing_to({0,0}, push);
    return std::abs(signed_bearing_delta(target,bearing)) <= 90 ? target : bearing;
}
Evader::Evader(EvaderOptions options) : options_(options) {
    if (options.evasion_ticks < 0 || options.cooldown_ticks < 0) throw std::invalid_argument("negative evasion duration");
    reset();
}
void Evader::reset() { state_ = EvaderState::IDLE; state_until_ = 0; rudder_sign_ = options_.initial_rudder_sign >= 0 ? 1 : -1; }
std::optional<Command> Evader::update(const WorldView& view) {
    if (state_ != EvaderState::IDLE && view.tick >= state_until_) {
        if (state_ == EvaderState::EVADING) { state_ = EvaderState::COOLDOWN; state_until_ = add_ticks(view.tick, options_.cooldown_ticks); }
        else state_ = EvaderState::IDLE;
    }
    bool hit = std::any_of(view.events.begin(), view.events.end(), [](const TickEvent& e) { return std::holds_alternative<HitEvent>(e); });
    if (hit && state_ != EvaderState::EVADING) {
        if (state_ == EvaderState::COOLDOWN) rudder_sign_ = -rudder_sign_;
        state_ = EvaderState::EVADING; state_until_ = add_ticks(view.tick, options_.evasion_ticks);
    }
    if (state_ == EvaderState::EVADING) { Command c; c.throttle = options_.throttle; c.rudder = rudder_sign_; return c; }
    return {};
}
std::string DutyCycle::choose(const WorldView& view, const Tracker&) {
    Tick cycle = std::max<Tick>(1, Tick(active_ticks)+passive_ticks);
    Tick phase = view.tick % cycle; if (phase < 0) phase += cycle;
    return phase < active_ticks ? "active" : "passive";
}
std::string PingWhenStale::choose(const WorldView& view, const Tracker& tracker) {
    bool ship = false;
    for (const auto& t : tracker.tracks()) if (t.kind == "ship") {
        ship = true;
        if (elapsed(view.tick, t.last_active_tick) >= stale_threshold_ticks) return "active";
    }
    return ship ? "passive" : "active";
}
const Track* ThreatList::nearest() const {
    if (tracks.empty()) return nullptr;
    return &*std::min_element(tracks.begin(), tracks.end(), [&](const Track& a, const Track& b) { return distance(me_pos,a.pos) < distance(me_pos,b.pos); });
}
const Track* ThreatList::farthest() const {
    if (tracks.empty()) return nullptr;
    return &*std::max_element(tracks.begin(), tracks.end(), [&](const Track& a, const Track& b) { return distance(me_pos,a.pos) < distance(me_pos,b.pos); });
}
const Track* ThreatList::by_id(int id) const { for (const auto& t : tracks) if (t.track_id == id) return &t; return nullptr; }
Intent Intent::engage(const Track& target) { Intent i; i.kind = IntentKind::ENGAGE; i.target = target; return i; }
Intent Intent::patrol(std::array<double,4> rect) { Intent i; i.kind = IntentKind::PATROL; i.rect = rect; return i; }
Intent Intent::retreat_to(Point point) { Intent i; i.kind = IntentKind::RETREAT_TO; i.point = point; return i; }
Intent Intent::hold() { return {}; }
Intent Intent::custom(Command command) { Intent i; i.kind = IntentKind::CUSTOM; i.command = std::move(command); return i; }
void TacticalBot::on_welcome(const Welcome& w) {
    welcome = w;
    TrackerOptions to; to.simulation_dt = w.simulation_dt;
    tracker = std::make_unique<Tracker>(w.ship_specs, w.tick_hz, to);
    auto rules = w.rules();
    PowerupConfig powerups = rules ? rules->powerups : PowerupConfig{};
    GunnerOptions go; go.powerups = powerups; go.simulation_dt = w.simulation_dt;
    gunner = std::make_unique<Gunner>(w.ship_specs, go);
    HelmOptions ho; ho.map_width = w.map.width; ho.map_height = w.map.height; ho.powerups = powerups;
    helm = std::make_unique<Helm>(w.ship_specs, ho);
    if (!evader) evader = std::make_unique<Evader>();
    on_tactical_welcome(w);
}
void TacticalBot::on_game_start(Tick, Point, double) {
    if (tracker) tracker->reset();
    if (gunner) gunner->reset();
    if (evader) evader->reset();
    if (sensor_policy) sensor_policy->reset();
    patrol_corner_ = 0;
}
Command TacticalBot::on_tick(const WorldView& view) {
    if (!tracker || !gunner || !helm || !evader || !welcome || !sensor_policy) return {};
    auto tracks = tracker->update(view); gunner->update(view);
    ThreatList threats{{}, view.me().pos};
    for (const auto& t : tracks) if (t.kind == "ship") threats.tracks.push_back(t);
    TacticalContext ctx{view, view.me(), welcome->ship_specs, *tracker, threats, double(welcome->map.width), double(welcome->map.height)};
    if (auto c = evader->update(view)) { c->sensor_mode = sensor_policy->choose(view, *tracker); return *c; }
    auto intent = decide(ctx);
    if (intent.kind == IntentKind::CUSTOM) return intent.command.value_or(Command{});
    auto cmd = intent_to_command(intent, ctx);
    cmd.sensor_mode = sensor_policy->choose(view, *tracker);
    const Track* target = intent.kind == IntentKind::ENGAGE && intent.target ? &*intent.target
        : intent.kind == IntentKind::HOLD ? nullptr : threats.nearest();
    if (target) gunner->attempt(cmd, view.me(), *target, view);
    return cmd;
}
Command TacticalBot::intent_to_command(const Intent& intent, const TacticalContext& ctx) {
    std::optional<Point> target;
    if (intent.kind == IntentKind::ENGAGE && intent.target) target = intent.target->pos;
    if (intent.kind == IntentKind::RETREAT_TO) target = intent.point;
    if (intent.kind == IntentKind::PATROL && intent.rect) {
        const auto& r = *intent.rect;
        std::array<Point,4> corners{{{r[0],r[1]}, {r[2],r[1]}, {r[2],r[3]}, {r[0],r[3]}}};
        if (distance(ctx.me.pos, corners[patrol_corner_]) < 25) patrol_corner_ = (patrol_corner_+1)%4;
        target = corners[patrol_corner_];
    }
    Command cmd;
    if (target) { auto motion = helm->steer_to_point(ctx.me, *target); cmd.throttle = motion.first; cmd.rudder = motion.second; }
    return cmd;
}
}
