#include <naval_sdk/naval_sdk.hpp>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace naval_sdk;
using namespace naval_sdk::tactical;
int checks = 0;
#define CHECK(...) do { ++checks; if (!(__VA_ARGS__)) throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " + #__VA_ARGS__); } while (false)
template<class F> void throws(F&& fn) { bool threw = false; try { fn(); } catch (const std::exception&) { threw = true; } CHECK(threw); }
void near(double a, double b) { CHECK(std::abs(a-b) <= 1e-8 * std::max({1.0,std::abs(a),std::abs(b)})); }
Json read(const std::filesystem::path& p) { std::ifstream f(p); CHECK(bool(f)); return Json::parse(f); }
Json fixture() { return read(std::filesystem::path(FIXTURE_DIR)/"protocol3.json"); }
WorldView view() { return WorldView::from_dict(fixture().at("tick")); }
ShipSpecs specs() { return Welcome::from_dict(fixture().at("welcome")).ship_specs; }
struct TempDir {
    std::filesystem::path path = std::filesystem::temp_directory_path()/
        ("naval-sdk-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TempDir() { CHECK(std::filesystem::create_directory(path)); }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(path,ec); }
};
void protocol() {
    auto f = fixture();
    auto w = Welcome::from_dict(f["welcome"]);
    CHECK(w.rules()->available_powerups.size() == 12);
    CHECK(w.rules()->powerups.overdrive_duration_ticks == 50);
    CHECK(w.rules()->powerups.raw.contains("heavy_shell_damage_mult"));
    auto configuration = w.configuration;
    configuration["ship_specs"]["shell_speed"] = 91;
    auto updated = w.with_configuration(configuration,"new-hash");
    CHECK(updated.ship_specs.shell_speed == 91); CHECK(w.ship_specs.shell_speed == 70);
    CHECK(updated.config_hash == "new-hash");
    for (const Json& invalid : std::vector<Json>{0, -1, true, 0.5, nullptr}) {
        auto bad = configuration; bad["tick_hz"] = invalid;
        throws([&]{ MatchConfiguration::from_dict(bad); });
    }
    throws([&]{ w.with_configuration(configuration, ""); });
    throws([]{ PowerupConfig::from_dict(Json{{"awacs_silent_confidence",1.1}}); });
    throws([]{ PowerupConfig::from_dict(Json{{"overdrive_speed_mult",0}}); });
    throws([]{ PowerupConfig::from_dict(Json{{"decoy_flare_distance_min",200}}); });
    CHECK(PowerupConfig::from_dict(Json{{"repair_drones_instant_hp",0}}).repair_drones_instant_hp == 0);
    for (auto invalid : {"2.0","4.0","3","3.0.0","3.-1","3.x"}) throws([&]{ check_protocol(invalid); });
    check_protocol("3.15"); check_protocol("03.0");
    auto v = view(); CHECK(v.nearest_contact() == nullptr); CHECK(!v.me().gun_cooldown_ticks_left);
    auto t = f["tick"]; t["self"]["gun_cooldown_ticks_left"] = 0; t["self"]["emp_ticks_left"] = -1;
    t["self"]["powerup_status"] = Json::array({{{"id","heavy_shell"}},{{"id","overdrive"},{"used",true},{"active_ticks_left",4}}});
    t["contacts"] = Json::array({{{"id","c1"},{"pos",{200,200}},{"bearing_deg",90},{"range",100}},
        {{"id","c2"},{"pos",{100,100}},{"bearing_deg",90},{"range",50}}});
    t["events"] = Json::array({{{"type","hit"},{"amount",5}},{{"type","hit"}},{{"type","future"},{"payload",42}},42,
        {{"type","shell_splash"},{"pos",{2,3}}},{{"type","powerup_activated"},{"own",true},{"powerup","heavy_shell"}}});
    v = WorldView::from_dict(t); CHECK(v.me().powerup_ready("heavy_shell")); CHECK(v.me().powerup_active("overdrive"));
    CHECK(v.me().emp_ticks_left == 0); CHECK(*v.me().gun_cooldown_ticks_left == 0); CHECK(v.nearest_contact()->id == "c2");
    CHECK(std::holds_alternative<HitEvent>(v.events[0])); CHECK(std::holds_alternative<Json>(v.events[1]));
    CHECK(std::holds_alternative<ShellSplashEvent>(v.events[4])); CHECK(std::holds_alternative<PowerupActivatedEvent>(v.events[5]));
    Command c; c.throttle = 2; CHECK(c.to_dict(1,"m")["throttle"] == 2); // Server owns clamping.
    c.fire_at({100,0},{0,0},Vec2{0,10}); CHECK(c.fire->bearing_deg > 90);
    c.throttle = std::numeric_limits<double>::infinity(); throws([&]{ c.to_dict(1); });
    c.throttle = 1e39; throws([&]{ c.to_dict(1); });
    c.throttle = 0; c.sensor_mode = "bad"; throws([&]{ c.to_dict(1); });
    near(bearing_to({0,0},{0,-1}),0); near(bearing_to({0,0},{1,0}),90);
    near(wrap_bearing(-360001),359); near(signed_bearing_delta(180,0),-180);
    CHECK(!lead_target({0,0},{100,0},{100,0},70));
    CHECK(lead_target({0,0},{0,0},{70,0},70) == std::optional<Point>({0,0}));
}
struct LifecycleBot : Bot {
    bool accept = true, crash = false, invalid = false;
    int starts = 0, ends = 0, welcomes = 0;
    std::vector<std::string> picks{"rapid_fire","heavy_shell"};
    bool accept_configuration(const Json&,const std::string&) override { return accept; }
    std::vector<std::string> choose_powerups(const Welcome&) override { return picks; }
    void on_welcome(const Welcome&) override { ++welcomes; }
    void on_game_start(Tick,Point,double) override { ++starts; }
    Command on_tick(const WorldView&) override {
        if (crash) throw std::runtime_error("callback failure");
        Command c; c.throttle = invalid ? std::numeric_limits<double>::quiet_NaN() : 0.5; return c;
    }
    bool on_game_over(const GameOver&) override { return ++ends < 2; }
};
void lifecycle() {
    auto f = fixture(); LifecycleBot b; Session s(b);
    auto out = s.handle(f["welcome"]); CHECK(out.size() == 2); CHECK(out[0]["type"] == "select_powerups");
    CHECK(out[1]["config_hash"] == f["welcome"]["config_hash"]);
    CHECK(s.handle(f["welcome"]).empty());
    b.picks.clear(); auto update = Json{{"type","configuration"},{"configuration",f["welcome"]["configuration"]},{"config_hash","changed"}};
    out = s.handle(update); CHECK(out.size() == 2); CHECK(out[0]["powerups"].empty());
    s.handle(f["game_start"]); CHECK(b.starts == 1); CHECK(b.match_id == "fixture-match-1");
    out = s.handle(f["tick"]); CHECK(out.size() == 1); CHECK(out[0]["throttle"] == 0.5); CHECK(out[0]["match_id"] == b.match_id);
    b.crash = true; out = s.handle(f["tick"]); CHECK(out[0]["throttle"] == 0); CHECK(b.diagnostics.callback_errors == 1);
    b.crash = false; b.invalid = true; out = s.handle(f["tick"]); CHECK(out[0]["throttle"] == 0); CHECK(b.diagnostics.callback_errors == 2);
    s.handle(Json::array()); auto broken = f["tick"]; broken["self"]["pos"] = Json::array();
    s.handle(broken); CHECK(b.diagnostics.malformed_frames == 2); CHECK(b.diagnostics.ticks == 3);
    s.handle(f["game_over"]); CHECK(!s.stop); CHECK(b.phase == "ended");
    out = s.handle({{"type","lobby"},{"tick",0}}); CHECK(out.size() == 1); CHECK(b.match_id.empty());
    s.handle(f["game_start"]); CHECK(b.starts == 2); s.handle(f["game_over"]); CHECK(s.stop);
    s.handle({{"type","error"},{"code","unauthorized"}}); CHECK(s.fatal); CHECK(b.diagnostics.rejected_commands["unauthorized"] == 1);
    LifecycleBot refusal; refusal.accept = false; Session sr(refusal); CHECK(sr.handle(f["welcome"]).empty()); CHECK(!sr.ready_sent);
    refusal.accept = true; refusal.picks = {"rapid_fire","rapid_fire"}; CHECK(sr.ready().empty());
    refusal.picks = {"future","heavy_shell"}; CHECK(sr.ready().empty());
    refusal.picks = {"rapid_fire"}; CHECK(sr.ready().empty());
    refusal.picks = {"rapid_fire","heavy_shell"}; CHECK(sr.ready().size() == 2);
    refusal.picks.clear(); auto old = update; old["configuration"]["capabilities"]["empty_loadout"] = false;
    CHECK(sr.handle(old).empty()); CHECK(!sr.ready_sent);
    auto bad_version = update; bad_version["configuration"]["protocol_version"] = "4.0";
    throws([&]{ sr.handle(bad_version); }); CHECK(sr.fatal);
    throws([&]{ b.raw_send(Json::object()); }); throws([&]{ b.raw_recv(); });
}
void tactical_tests() {
    auto sp = specs(); auto v = view(); v.self_state.pos = {0,0};
    for (int pacing : {5,10,20,60}) {
        Tracker tracker(sp,pacing);
        for (int i = 0; i < 100; ++i) {
            v.tick = i; v.contacts = {Contact{"unstable-"+std::to_string(i),"ship",{100+0.9*i,0},90,100,1}};
            tracker.update(v);
        }
        CHECK(tracker.tracks().size() == 1); near(tracker.tracks()[0].vel[0],9);
        v.tick = 100; v.contacts[0].range.reset(); v.contacts[0].pos = {0,0}; tracker.update(v);
        CHECK(tracker.get(1)->source == "passive"); near(tracker.get(1)->pos[0],190);
        PingWhenStale policy(1); CHECK(policy.choose(v,tracker) == "active");
        v.tick = 150; v.contacts.clear(); CHECK(tracker.update(v).empty()); tracker.reset();
    }
    v.tick = 0; v.contacts.clear();
    Track target{1,"ship",{100,0},{100,0},{0,0},0,0,0,1,"active"};
    Gunner gun(sp); CHECK(gun.solve(v.me(),target,v));
    Command cmd; CHECK(gun.attempt(cmd,v.me(),target,v)); CHECK(gun.next_fire_tick() == 15);
    v.tick = 1; gun.update(v); CHECK(gun.can_fire(v,v.me())); // Shot rejected: ammo didn't decrease.
    gun.note_fired(1); CHECK(!gun.can_fire(v,v.me()));
    v.self_state.gun_cooldown_ticks_left = 0; gun.update(v); CHECK(gun.can_fire(v,v.me()));
    v.self_state.powerup_status = {{"rapid_fire",false,0},{"long_range_salvo",false,0},{"heavy_shell",false,0}};
    CHECK(gun.effective_weapons(v.me(),"rapid_fire").cooldown == 8);
    v.self_state.emp_ticks_left = 1; CHECK(gun.effective_weapons(v.me(),"rapid_fire").cooldown == 15);
    near(gun.effective_weapons(v.me(),"long_range_salvo").speed,112);
    target.pos = {400,0}; CHECK(!gun.solve(v.me(),target,v)); CHECK(gun.solve(v.me(),target,v,"long_range_salvo"));
    target.pos = {35,0}; CHECK(gun.solve(v.me(),target,v)); CHECK(!gun.solve(v.me(),target,v,"heavy_shell"));
    target.pos = {100,0}; v.self_state.heading_deg = 90; v.self_state.speed = 65;
    CHECK(!gun.solve(v.me(),target,v)); v.self_state.speed = 0;
    gun.reset(); CHECK(gun.next_fire_tick() == 0);
    Helm helm(sp); v.self_state.pos = {5,350}; v.self_state.heading_deg = 0;
    auto steering = helm.steer_to_bearing(v.me(),270); CHECK(steering.second > 0);
    Evader evader; v.tick = 1; v.events = {HitEvent{5}};
    CHECK(evader.update(v)->rudder == 1); v.tick = 16; v.events.clear(); CHECK(!evader.update(v));
    v.events = {HitEvent{1}}; CHECK(evader.update(v)->rudder == -1); evader.reset(); CHECK(evader.state() == EvaderState::IDLE);
    struct Hunter : TacticalBot {
        Intent decide(const TacticalContext& ctx) override { if (auto t = ctx.threats.nearest()) return Intent::engage(*t); return Intent::hold(); }
    } hunter;
    auto f = fixture(); Session session(hunter); session.handle(f["welcome"]); session.handle(f["game_start"]);
    auto tick = f["tick"]; tick["contacts"] = Json::array({{{"id","a"},{"kind","ship"},{"pos",{600,500}},{"bearing_deg",90},{"range",100},{"confidence",1}}});
    auto out = session.handle(tick); CHECK(out[0].contains("fire")); CHECK(hunter.tracker->tracks().size() == 1);
    session.handle(f["game_start"]); CHECK(hunter.tracker->tracks().empty()); CHECK(hunter.gunner->next_fire_tick() == 0);
}
void recording() {
    TempDir dir; auto path = dir.path/"bot.jsonl"; auto f = fixture();
    {
        BotRecorder recorder(path);
        throws([&]{ BotRecorder duplicate(path); });
        recorder.record({{"type","hello"},{"token","secret"}});
        auto welcome = f["welcome"]; welcome["nested"] = {{{"Authorization","secret"},{"password","secret"}}};
        recorder.record(welcome); recorder.record(f["game_start"]); recorder.record(f["tick"]); recorder.record(f["game_over"]);
    }
    std::ifstream input(path); std::string text((std::istreambuf_iterator<char>(input)),{});
    CHECK(text.find("secret") == std::string::npos); CHECK(text.find("[redacted]") != std::string::npos);
    Bot bot; auto decisions = replay(bot,path); CHECK(decisions.size() == 1); CHECK(decisions[0].tick == 1);
    CHECK(decisions[0].command == Command{}.to_dict(1,"fixture-match-1")); CHECK(!bot.running());
    std::ofstream(dir.path/"bad.jsonl") << "{}\n"; throws([&]{ replay(bot,dir.path/"bad.jsonl"); }); CHECK(!bot.running());
}
void cli() {
    TempDir dir; auto path = dir.path/"participant.env";
    std::ofstream(path) << "# test\nexport BATTLE_SERVER_URL='wss://example.org/bot'\nBATTLE_BOT_TOKEN=\"literal$(no-exec)`test`\" # comment\n";
    ConnectionArguments args; args.env_file = path;
    auto options = connection_options(args); CHECK(options.token == "literal$(no-exec)`test`"); CHECK(options.url == "wss://example.org/bot");
    args.host = "::1"; args.port = 9999; CHECK(connection_options(args).url == "ws://[::1]:9999/bot");
    args.url = "ws://localhost/bot"; throws([&]{ connection_options(args); });
    args.host.reset(); args.port.reset();
    for (auto url : {"https://host/bot","ws://user:secret@host/bot","ws://host/bot?token=secret","ws://host:0/bot","ws://host:65536/bot","ws://host/other","ws://[bad]/bot","ws://host/bot#secret"}) {
        args.url = url; throws([&]{ connection_options(args); });
    }
    std::ofstream(path) << "BATTLE_SERVER_URL=ws://localhost/bot\n"; args.url.reset(); throws([&]{ connection_options(args); });
    std::ofstream(path) << "BATTLE_SERVER_URL=ws://localhost/bot\nBATTLE_BOT_TOKEN=x\nBATTLE_BOT_TOKEN=y\n"; throws([&]{ connection_options(args); });
}
void parity() {
    auto golden = read(std::filesystem::path(FIXTURE_DIR)/"python_parity.json");
    for (const auto& row : golden.at("math")) {
        Point a = row["a"].get<Point>(), b = row["b"].get<Point>(), velocity = row["velocity"].get<Point>();
        near(distance(a,b),row["distance"]); near(bearing_to(a,b),row["bearing"]);
        auto aim = lead_target(a,b,velocity,row["speed"]);
        if (row["aim"].is_null()) CHECK(!aim); else { CHECK(aim); near((*aim)[0],row["aim"][0]); near((*aim)[1],row["aim"][1]); }
    }
    auto sp = specs(); Tracker tracker(sp); Gunner gun(sp); Helm helm(sp); Evader evader;
    for (const auto& row : golden.at("tactical")) {
        auto v = WorldView::from_dict(row["view"]); auto tracks = tracker.update(v); gun.update(v);
        CHECK(tracks.size() == row["tracks"].size());
        for (std::size_t i = 0; i < tracks.size(); ++i) {
            const auto& t = tracks[i]; const auto& expected = row["tracks"][i];
            CHECK(t.track_id == expected["track_id"]); CHECK(t.source == expected["source"]); CHECK(t.kind == expected["kind"]);
            for (int axis = 0; axis < 2; ++axis) { near(t.pos[axis],expected["pos"][axis]); near(t.vel[axis],expected["vel"][axis]); }
        }
        auto steering = helm.steer_to_bearing(v.me(),row["target_bearing"]);
        near(steering.first,row["steering"][0]); near(steering.second,row["steering"][1]);
        auto evade = evader.update(v);
        if (row["evade"].is_null()) CHECK(!evade); else CHECK(evade->to_dict(v.tick,v.match_id) == row["evade"]);
        if (!tracks.empty()) {
            auto shot = gun.solve(v.me(),tracks.front(),v);
            if (row["shot"].is_null()) CHECK(!shot);
            else { CHECK(shot); near(shot->bearing_deg,row["shot"]["bearing_deg"]); near(shot->range,row["shot"]["range"]); }
        }
    }
}
int main(int argc,char* argv[]) {
    try {
        if (argc != 2) throw std::runtime_error("specify a test suite");
        std::map<std::string,void(*)()> suites{{"protocol",protocol},{"lifecycle",lifecycle},{"tactical",tactical_tests},{"recording",recording},{"cli",cli},{"parity",parity}};
        suites.at(argv[1])(); std::cout << argv[1] << ": " << checks << " checks passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
