# Tactical toolkit

Subclass `naval_sdk::tactical::TacticalBot` and implement
`Intent decide(const TacticalContext&)`. See the complete
[hunter example](../examples/hunter_bot.cpp). The context includes the original
view, own ship, specs, tracker, map dimensions, and ship-only `ThreatList`.
Threat queries `nearest()`, `farthest()`, and `by_id()` return pointers into that
context; `Intent::engage` copies the selected track.

## Intents and execution order

| Factory | Behavior |
| --- | --- |
| `Intent::engage(track)` | Steer toward and try to shoot that track |
| `Intent::patrol({x1,y1,x2,y2})` | Cycle rectangle corners; opportunistically shoot nearest threat |
| `Intent::retreat_to(point)` | Steer to a point; opportunistically shoot nearest threat |
| `Intent::hold()` | Zero controls, no firing |
| `Intent::custom(command)` | Return the exact command without sensor/gunner overlays |

Each tick updates Tracker and Gunner first. Evader can preempt `decide` entirely.
Otherwise intent navigation uses Helm's wall override, then sensor and firing
overlays are applied. Custom commands bypass those overlays. The SDK's runtime
still validates every resulting command.

Subsystems are `unique_ptr` members initialized in `on_welcome`. Override
`on_tactical_welcome` to configure them or change `sensor_policy`. Call the base
`on_game_start` if overriding it: that resets tracking IDs/history, gun cooldown,
evasion state, sensor state, and the patrol corner between matches.

## Tracker

`Tracker(specs, tick_hz, TrackerOptions)` associates transient contacts with
persistent tracks. `update(view)` returns copies sorted by stable track ID;
`tracks()`, `get(id)`, and `reset()` expose the current set. Active range fixes can
spawn tracks; passive bearings can only update existing tracks. Tracks contain
position, observed position, velocity in units per second, observation ticks,
confidence, kind, and source (`active`, `passive`, `dead_reckoned`).

Options default to simulation step 0.1, active distance gate 60, passive bearing
gate 20°, velocity smoothing alpha 0.3, velocity window 10 ticks, and staleness
40 ticks. Simulation time drives velocity; `tick_hz` is pacing only. Association
is the same greedy heuristic as Python and can confuse nearby targets.

## Gunner

`Gunner(specs, GunnerOptions)` models ammo, cooldown, intercept range, recent
active fixes, and a projected self-splash guard. Defaults: splash margin 1.5,
maximum active-fix age 5 ticks, recent fixes required, simulation step 0.1.
Supply the acknowledged `PowerupConfig` through the options.

- `update(view)` reconciles authoritative gun telemetry, or falls back to ammo
  consumption to detect rejected shots on older protocol 3 servers.
- `solve(me, track, view, optional_activation)` returns an optional `FireSolution`
  without mutating state.
- `attempt(command, me, track, view)` updates state, attaches a feasible shot,
  and records its cooldown.
- `note_fired(tick, optional_cooldown, optional_ammo)` records a manually committed shot.
- `can_fire(view, me)`, `next_fire_tick()`, and `reset()` manage firing readiness.
- `effective_weapons(me, optional_activation)` returns speed, maximum range,
  splash radius, and rounded cooldown.

Known active powerups and a ready same-command activation affect calculations.
Rapid fire, heavy shell, long-range salvo, and EMP are included. Self-splash
projection accounts for discrete simulation steps and own-ship motion.

## Helm, evasion, and sensors

`Helm::steer_to_bearing` and `steer_to_point` return `{throttle, rudder}`. Optional
arguments control wall avoidance and desired throttle. `HelmOptions` provides
map dimensions (700 × 700), wall margin (30), turn aggression (30°), alignment
threshold (10°), minimum turn throttle (0.55), and powerup constants. The wall
predictor accounts for speed, braking, turn rate, and overdrive.

`Evader::update(view)` returns an optional overriding command on hits. Defaults
are 15 ticks of evasion, 10 cooldown ticks, full throttle, and positive rudder.
A new hit in cooldown reverses the rudder. `reset()` restores the initial state.

Implement `SensorPolicy::choose(view, tracker)` and optionally `reset()`, or use:

| Policy | Behavior |
| --- | --- |
| `AlwaysActive` | Active every tick |
| `AlwaysPassive` | Passive every tick |
| `DutyCycle(active_ticks=10, passive_ticks=20)` | Repeating schedule |
| `PingWhenStale(threshold=4)` | Active if no ship track exists or any ship's active fix is stale |

Passive reports do not refresh the age of an active range fix.
