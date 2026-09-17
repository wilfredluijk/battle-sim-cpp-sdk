# battle-sim C++ SDK

[![CI](https://github.com/wilfredluijk/battle-sim-cpp-sdk/actions/workflows/ci.yml/badge.svg)](https://github.com/wilfredluijk/battle-sim-cpp-sdk/actions/workflows/ci.yml)

C++17 SDK for building bots for the battle-sim naval simulator, compatible with
server protocol **3.x**. A native port of the
[Python SDK](https://github.com/wilfredluijk/battle-sim-python-sdk), including its
tactical toolkit, configuration agreement, multi-match lifecycle, diagnostics,
and credential-redacted recording/replay. No Python runtime is needed by a bot.

## Build

Requires CMake 3.20+, a C++17 compiler, Boost 1.74+, OpenSSL 1.1.1+, and
nlohmann/json 3.10+. The build uses installed dependencies and does not download code.

Ubuntu / Debian:

```sh
sudo apt-get install build-essential cmake libboost-dev libssl-dev nlohmann-json3-dev
git clone https://github.com/wilfredluijk/battle-sim-cpp-sdk.git
cd battle-sim-cpp-sdk
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

macOS: `brew install cmake boost openssl@3 nlohmann-json`, then use the same CMake
commands. If OpenSSL is not detected, add
`-DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)"` when configuring.

Windows with Visual Studio 2022 and [vcpkg](https://github.com/microsoft/vcpkg):

```powershell
vcpkg install boost-beast boost-asio openssl nlohmann-json --triplet x64-windows
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Your first bot

```cpp
#include <naval_sdk/naval_sdk.hpp>

class MyBot : public naval_sdk::Bot {
public:
    naval_sdk::Command on_tick(const naval_sdk::WorldView&) override {
        naval_sdk::Command command;
        command.throttle = 0.6;
        command.rudder = 0.2;
        return command;
    }
};

int main() {
    MyBot bot;
    naval_sdk::RunOptions options;
    options.name = "my-cpp-bot";
    naval_sdk::run(bot, options);
}
```

Set `BATTLE_SERVER_URL` and `BATTLE_BOT_TOKEN` to the endpoint and participant
credential provided by your operator. The default endpoint is
`ws://localhost:7878/bot`. The examples also accept a participant environment file:

```sh
./build/my_bot --env-file /path/to/participant.env --name my-cpp-bot
./build/hunter_bot --url ws://localhost:7878/bot --matches 2 --record match.jsonl
./build/naval-sdk replay match.jsonl
```

Use `--help` for all options. Credentials come from the environment or an explicitly
chosen file; there is no command-line token option. `wss://` verifies the server
certificate and hostname. Use `--ca-file` for a private CA or a platform without an
OpenSSL default trust store.

## Add to your project

With a checkout at `external/battle-sim-cpp-sdk`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_bot LANGUAGES CXX)
add_subdirectory(external/battle-sim-cpp-sdk)
add_executable(my_bot main.cpp)
target_link_libraries(my_bot PRIVATE naval_sdk::naval_sdk)
```

Or install the SDK and use its exported CMake package:

```sh
cmake --install build --prefix "$HOME/.local"
```

```cmake
find_package(naval_sdk 0.1 CONFIG REQUIRED)
target_link_libraries(my_bot PRIVATE naval_sdk::naval_sdk)
```

Set `CMAKE_PREFIX_PATH` to the installation prefix if needed. The dependencies must
also be available when configuring a consuming project.

## Guides

- [API and lifecycle](docs/api.md): callbacks, commands, models, configuration, diagnostics.
- [Tactical toolkit](docs/tactics.md): tracking, aiming, steering, sensors, evasion, intents.
- [Recording and replay](docs/recording.md): Python-compatible bot-view files.
- [Migrating from Python](docs/migration.md): types, ownership, async behavior, differences.
- [Development and validation](CONTRIBUTING.md): tests, reference fixtures, local Rust integration.

The source headers in [`include/naval_sdk`](include/naval_sdk) are the complete
public declaration reference. [`examples/hunter_bot.cpp`](examples/hunter_bot.cpp)
shows a bot that tracks, pursues, and fires at the nearest threat.

## Compatibility

Ported from Python SDK commit
[`816fa9b`](https://github.com/wilfredluijk/battle-sim-python-sdk/commit/816fa9baba75c7294561a6f111aa2735f4bbdd01).
The tests include 410 Python-generated comparison cases, WebSocket/TLS loopback
tests, and an opt-in test against the actual Rust server. The toolkit is a set of
heuristics; steering and evasion do not guarantee collision avoidance or victory.

MIT licensed. Original attribution is retained in [LICENSE](LICENSE) and [NOTICE](NOTICE).
