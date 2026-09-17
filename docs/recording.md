# Recording and replay

`BotRecorder` writes Python-compatible `naval-sdk-bot-views` version 1 JSONL files.
The constructor creates the file exclusively and refuses to overwrite an existing
path. Its destructor closes it. Only recognized inbound lifecycle frames are
recorded; outbound `hello` authentication is never recorded. Nested keys containing
`token`, `password`, `credential`, or `authorization` are redacted, case-insensitively.
This field filter cannot identify arbitrary secrets in free-form text from a server.

```cpp
MyBot bot;
naval_sdk::BotRecorder recording("match.jsonl");
naval_sdk::RunOptions options;
options.recorder = &recording;
naval_sdk::run(bot, options);
```

Replay streams observations through the same lifecycle dispatcher, without a
server or credentials. It does not rerun physics: alternate decisions still see
the original observations.

```cpp
MyBot bot;
naval_sdk::replay(bot, "match.jsonl", [](const naval_sdk::ReplayDecision& decision) {
    // decision.match_id, decision.tick, decision.command
});
```

The two-argument overload returns `std::vector<ReplayDecision>` for small files.
The callback overload keeps memory bounded. Each replay starts with clean runtime
diagnostics and connection state, and refuses to overlap another run/replay on the
same bot. User-defined state still belongs to the bot's lifecycle callbacks.

`./build/naval-sdk replay match.jsonl` prints commands from the default hold-station
bot. To evaluate your own strategy, use the library overload with your bot subclass.
