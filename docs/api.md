# API and lifecycle

Include `<naval_sdk/naval_sdk.hpp>` or individual headers. Public symbols live in
`naval_sdk`; tactical symbols live in `naval_sdk::tactical`. `Json` aliases
`nlohmann::json`, `Point`/`Vec2` are `std::array<double, 2>`, and `Tick` is a signed
64-bit integer. Bearings increase clockwise: north is 0°, east is 90°.

## Connection

`run(Bot&, RunOptions)` blocks until the connection closes or the bot opts out.
`run_async(Bot&, RunOptions)` returns `std::future<std::optional<GameOver>>` and
runs the same lifecycle on a worker thread. Keep the bot and optional recorder
alive until the future completes. Callbacks run serially on the runtime thread.
Do not access mutable bot state from another thread during a run.

| RunOptions field | Default / behavior |
| --- | --- |
| `url` | Explicit URL, otherwise `BATTLE_SERVER_URL`, otherwise host/port/path |
| `host`, `port`, `path` | `localhost`, `7878`, `/bot` |
| `name`, `version` | `bot`, `naval-sdk-cpp/0.1.0` |
| `token` | `BATTLE_BOT_TOKEN` when unset; explicit empty string overrides environment |
| `recorder` | Optional non-owning `BotRecorder*` |
| `reconnect_attempts` | 0; bounded retries only outside a running match |
| `reconnect_delay` | 1 second; must be finite and between 0 and 60 |
| `connect_timeout` | 10 seconds; positive, at most 300; bounds setup operations, initial welcome, writes |
| `ca_file` | Additional PEM CA certificates; hostname and certificate checks always remain enabled |

URLs must use `ws` or `wss`, with no user information, query, fragment, or whitespace.
The hosted workshop server is `wss://93.190.187.250/bot` and requires the participant
token assigned by the operator.
The managed transport limits incoming messages to 1 MiB and handles WebSocket
ping/pong, fragmentation, and close frames through Boost.Beast. DNS cancellation
ultimately depends on the platform resolver. The SDK does not provide a forced
stop API; return false from `on_game_over` to leave cleanly. Dropping the async
future may block until its worker finishes.

## Lifecycle and callbacks

1. Connect and send `hello` with name, SDK version, and participant token.
2. Parse `welcome`, validate protocol 3.x and the authoritative configuration.
3. Call `on_welcome`, `accept_configuration`, and `choose_powerups`.
4. Send a validated loadout, then `ready` with the exact server config hash.
5. Refresh per-match specs if changed; deliver `game_start` and ticks.
6. Send one validated command for each accepted tick with its exact match ID.
7. Deliver `game_over`. Unless the callback returns false, a later lobby triggers
   loadout selection and readiness again.

| Callback | Default / purpose |
| --- | --- |
| `accept_configuration(const Json&, const std::string&) -> bool` | True; false remains unready |
| `on_welcome(const Welcome&)` | Observe acknowledged rules; also called when rules/specs change |
| `choose_powerups(const Welcome&) -> vector<string>` | Empty; select zero or two distinct catalog IDs |
| `on_game_start(Tick, Point, double)` | Positional start hook |
| `on_game_start_event(const GameStart&)` | Delegates to the positional hook |
| `on_tick(const WorldView&) -> Command` | Hold station with active sensors |
| `on_game_over(const GameOver&) -> bool` | True continues; false disconnects |
| `on_lobby(Tick)` | Reset user match state before the next readiness |
| `on_error(const string& code, const string& message)` | Observe server rejections |
| `on_tick_timing(const TickTiming&)` | Callback plus serialization time, excluding network latency |
| `on_disconnect(const DisconnectInfo&)` | Observe close code/reason and interrupted phase |

Callback exceptions increment `diagnostics.callback_errors`. A failed or invalid
tick command becomes a hold-station command. Deadlines are measured, not enforced
by forcibly interrupting C++ code. Invalid loadouts and refused configurations
leave the bot unready. Malformed frames are counted and ignored; an incompatible
protocol throws `ProtocolMismatch`. Authentication, name, and rate-limit errors
end the session without retrying. Unknown message types are ignored.

`welcome`, `last_tick`, `match_id`, `phase`, and `diagnostics` are available on the
bot. Phases are `disconnected`, `connecting`, `lobby`, `running`, and `ended`.
`raw_send(Json)` is available from runtime callbacks; the managed reader belongs
to `run`, so `raw_recv()` throws. For custom transports, dispatch frames through
`Session::handle` and send the returned JSON messages yourself.

## Models and commands

Models have `from_dict(const Json&)` factories. Accessors returning pointers refer
to data owned by their parent object; copy it before retaining it across updates.

| Type | Contents / conveniences |
| --- | --- |
| `Welcome` | Bot/ship IDs, map, specs, catalog, simulation step, protocol/hash, raw configuration; `rules()` returns typed rules |
| `MatchConfiguration` | Revision, timing/deadline, map/specs, sensors, all powerup constants, catalog, timeout, wall damage, raw JSON |
| `SelfState` | Position, heading, speed, hull/ammo, controls, loadout/status, optional authoritative gun cooldown, EMP duration |
| `Contact` | Transient ID, kind, position, bearing, optional range, confidence |
| `WorldView` | Tick, deadline, self, contacts, events, match ID; `me()` and `nearest_contact()` |
| `GameStart` | Tick, starting position/heading, optional refreshed specs, simulation step, match ID |
| `GameOver` | Optional winner, final tick, replay ID |
| `TickEvent` | Variant of `HitEvent`, `ShellSplashEvent`, `PowerupActivatedEvent`, or raw JSON |

Unknown or malformed individual events remain JSON so they do not discard the
whole tick. Unknown future configuration fields remain available through `raw`.
Missing own-ship cooldown telemetry remains `std::nullopt`, distinct from zero.

`Command` contains `throttle`, `rudder`, `sensor_mode` (`active`/`passive`), optional
`FireCommand`, and optional `activate_powerup`. Numbers sent to the server must
fit finite f32 values. Control clamping and final gameplay legality remain server
responsibilities. Serialize with `command.to_dict(tick, match_id)`.

```cpp
naval_sdk::Command command;
command.throttle = 0.8;
command.fire_at(target_position, view.me().pos, target_velocity,
                welcome.ship_specs.shell_speed);
if (view.me().powerup_ready("rapid_fire")) command.activate_powerup = "rapid_fire";
```

`fire_at(target, shooter, optional_velocity, shell_speed, optional_range, lead)`
defaults to origin, no velocity, 70 units/second, computed range, and lead enabled.
Always supply your own position and the current server shell speed. It aims but
does not apply Gunner's cooldown, range, or self-splash checks.

## Powerups

`PowerupConfig` exposes every constant from Python with the same snake_case field
name and default. Use acknowledged rules instead of assuming server defaults.

| Catalog ID | Effect |
| --- | --- |
| `overdrive` | Speed, acceleration, and turn multipliers |
| `reinforced_hull` | Damage reduction |
| `repair_drones` | Immediate and periodic hull repair |
| `smoke_screen` | Smoke radius and duration |
| `rapid_fire` | Reduced gun cooldown |
| `heavy_shell` | Increased splash radius and damage |
| `long_range_salvo` | Increased shell range and speed |
| `awacs_scan` | Extended sensing and silent-contact parameters |
| `silent_running` | Reduced active detection range |
| `counter_battery_trace` | Armed shot-reveal interval |
| `emp_burst` | Area EMP and gun cooldown multiplier |
| `decoy_flare` | Decoy lifetime and distance bounds |

`me.powerup(id)` returns a status pointer or null. `powerup_ready(id)` checks that a
selected powerup is unused; `powerup_active(id)` checks remaining active ticks.
Unknown future catalog IDs can be selected and activated when the server advertises
them; the current tactical helpers only model known effects.
