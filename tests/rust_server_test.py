"""Opt-in test against a local Rust battle-sim server; never a production endpoint."""
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request

binary = os.environ.get("BATTLE_SIM_SERVER")
if not binary:
    print("SKIP: set BATTLE_SIM_SERVER to a local naval-server binary")
    sys.exit(0)
client = str(Path(sys.argv[1]).resolve())
with socket.socket() as probe:
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]
password = "synthetic-cpp-integration-password"
token = None


def request(method, path, body=None):
    data = None if body is None else json.dumps(body).encode()
    headers = {"Content-Type": "application/json"}
    if token:
        headers["Authorization"] = "Bearer " + token
    req = urllib.request.Request(f"http://127.0.0.1:{port}" + path, data=data, headers=headers, method=method)
    with urllib.request.urlopen(req, timeout=3) as response:
        raw = response.read()
        return json.loads(raw) if raw else None


def wait_for(predicate):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        try:
            value = predicate()
            if value:
                return value
        except (OSError, ValueError):
            pass
        time.sleep(0.025)
    raise AssertionError("server did not reach expected lifecycle state")


def ready():
    state = request("GET", "/api/room")
    return state if len(state["bots"]) == 2 and all(b["ready"] for b in state["bots"]) else None


with tempfile.TemporaryDirectory(prefix="naval-rust-") as directory:
    directory = Path(directory)
    env = {**os.environ, "BATTLE_ADMIN_PASSWORD": password}
    for key in ("BATTLE_ADMIN_PASSWORD_FILE", "BATTLE_BOT_CREDENTIALS_FILE", "BATTLE_BOT_TOKEN", "BATTLE_SERVER_URL"):
        env.pop(key, None)
    clients = []
    with (directory / "server.log").open("w") as log:
        server = subprocess.Popen([str(Path(binary).resolve()), "--port", str(port), "--tick-hz", "20",
            "--tick-deadline-ms", "40", "--allow-unauthenticated-bots", "--replay-dir", str(directory/"replays")],
            env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            token = wait_for(lambda: request("POST", "/api/login", {"password": password}))["token"]
            for i in range(2):
                # The test client takes its name from a dedicated synthetic environment setting.
                child_env = {**env, "NAVAL_TEST_BOT_NAME": f"cpp-integration-{i}"}
                clients.append(subprocess.Popen([client, f"ws://127.0.0.1:{port}/bot", "rust", str(directory/f"bot-{i}.jsonl")],
                    env=child_env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True))
            state = wait_for(ready)
            config = state["config"]
            config["shell_speed"] = 91
            request("PUT", "/api/room/config", config)
            wait_for(ready)
            for number in (1, 2):
                request("POST", "/api/room/start", {})
                time.sleep(0.6)
                request("POST", "/api/room/abort", {})
                if number == 1:
                    request("POST", "/api/room/reset", {})
                    wait_for(ready)
            for process in clients:
                output, error = process.communicate(timeout=8)
                assert process.returncode == 0, error
                result = json.loads(output)
                assert result["exception"] is False and result["rounds"] == 2 and result["ticks"] >= 6, result
                assert result["malformed"] == result["callback_errors"] == 0 and not result["rejected"], result
                assert result["telemetry"] and result["replayed"] == result["ticks"], result
                assert len({s["match_id"] for s in result["starts"]}) == 2, result
                assert all(s["shell_speed"] == 91 for s in result["starts"]), result
            print("PASS real Rust server: configuration update, telemetry, two rounds, recording/replay")
        except BaseException:
            print((directory / "server.log").read_text(), file=sys.stderr)
            raise
        finally:
            for process in clients + [server]:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)
