# Migrating from Python

This port targets Python SDK commit `816fa9baba75c7294561a6f111aa2735f4bbdd01`.
It preserves protocol 3 frames, lifecycle callbacks, twelve powerup definitions,
the tactical algorithms, and recording format. C++ uses native types and ownership.

| Python | C++ |
| --- | --- |
| `from naval_sdk import ...` | `#include <naval_sdk/naval_sdk.hpp>` |
| `None` / optional value | `std::optional<T>`; use `has_value()` or boolean check |
| `(x, y)` | `Point{x, y}` / `Vec2{x, y}` |
| Dictionary | `Json` (`nlohmann::json`) |
| `view.me` | `view.me()` |
| `welcome.rules` | `welcome.rules()` returns `optional<MatchConfiguration>` |
| `tracker.tracks` | `tracker.tracks()` returns a vector of copies |
| `Command(throttle=0.6)` | `Command c; c.throttle = 0.6;` |
| Keyword arguments | Options structs or positional method arguments |
| `with BotRecorder(path)` | `BotRecorder recording(path);` with scope-based destruction |
| `run(bot, name="x")` | `RunOptions o; o.name="x"; run(bot,o);` |
| `await run_async(bot)` | `run_async(bot).get()`; a worker thread, not a coroutine |
| `replay(bot,path)` iterator | Callback-based replay, or vector overload |

The C++ API requires synchronous, correctly typed callback return values. Return
`Command{}` to hold station and `true` to continue after a match. References passed
to callbacks are only valid for that callback. Retain copies of observations if
needed. Own subsystem replacements with `std::unique_ptr`.

Intentional hardening and platform differences:

- Typed numeric JSON is required. Boolean or string values are not coerced into
  numbers, and integer fields reject fractions and overflow.
- Public structs are value types and can be mutated; parsing and outbound command
  serialization validate values. Construct tactical helpers with valid specs and
  options, preferably from the welcome message.
- Protocol/authentication failures stop promptly. Bounded retries never reconnect
  an active match. A connection loss does not resume the previous ship.
- `run_async` owns a worker thread. Keep objects alive and avoid concurrent access.
  The runtime cannot forcibly stop a callback or detach a running bot safely.
- `raw_recv` is deliberately unavailable in the managed runtime. `Session` supports
  applications that supply their own transport.
- Diagnostics are structured counters and callbacks. The SDK itself does not log
  frames, tokens, or callback exception messages.
- CLI parsing supports separate option/value arguments. Environment files are
  parsed as UTF-8 data; shell substitutions and variable expansion are never run.
- Replay resets diagnostics and connection state before dispatch. It stops on fatal
  authentication/name errors as well as a false game-over callback.

The same tactical limitations as the reference apply: greedy association, fixed
observation replay, constant-velocity intercepts, and heuristic wall/evasion logic.
