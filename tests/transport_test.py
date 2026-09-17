"""Loopback WebSocket interoperability, failures, retries, and TLS verification."""
import asyncio
import copy
import json
import os
from pathlib import Path
import shutil
import ssl
import subprocess
import sys
import tempfile

from websockets.asyncio.server import serve
from websockets.exceptions import ConnectionClosed

CLIENT = str(Path(sys.argv[1]).resolve())
FIXTURE = json.loads((Path(__file__).parent / "fixtures/protocol3.json").read_text())


async def exercise(mode, directory, tls=None, ca=None, host="127.0.0.1"):
    connections = 0
    errors = []

    async def handler(ws):
        nonlocal connections
        connections += 1
        try:
            hello = json.loads(await ws.recv())
            assert hello["type"] == "hello" and hello["token"] == "synthetic-cpp-test-token"
            if mode == "auth":
                await ws.send(json.dumps(dict(type="error", code="unauthorized", message="test")))
                await ws.wait_closed()
                return
            if mode == "retry" and connections < 3:
                await ws.close(1012, "restart")
                return
            if mode == "timeout":
                # Malformed frames must not extend the initial welcome deadline.
                for _ in range(20):
                    await ws.send("[]")
                    await asyncio.sleep(0.1)
                return
            if mode == "protocol":
                welcome = copy.deepcopy(FIXTURE["welcome"])
                welcome["protocol_version"] = "4.0"
                await ws.send(json.dumps(welcome))
                await ws.wait_closed()
                return
            for frame in ("not json", "[]", "null", b"binary"):
                await ws.send(frame)
            await ws.send(json.dumps(FIXTURE["welcome"]))
            assert json.loads(await ws.recv())["type"] == "select_powerups"
            assert json.loads(await ws.recv())["type"] == "ready"
            config = copy.deepcopy(FIXTURE["welcome"]["configuration"])
            config["ship_specs"]["shell_speed"] = 91
            await ws.send(json.dumps(dict(type="configuration", configuration=config, config_hash="changed")))
            assert json.loads(await ws.recv())["powerups"] == ["rapid_fire", "heavy_shell"]
            assert json.loads(await ws.recv())["config_hash"] == "changed"
            for round_number in (1, 2):
                start = copy.deepcopy(FIXTURE["game_start"])
                start["ship_specs"]["shell_speed"] = 91
                start["match_id"] = f"match-{round_number}"
                await ws.send(json.dumps(start))
                if mode == "active-close":
                    await ws.close(1011, "dropped during match")
                    return
                tick = copy.deepcopy(FIXTURE["tick"])
                tick["match_id"] = start["match_id"]
                tick["self"]["gun_cooldown_ticks_left"] = 0
                await ws.send(json.dumps(tick))
                command = json.loads(await ws.recv())
                assert command["match_id"] == start["match_id"] and command["tick"] == 1
                assert command["throttle"] == (0 if mode == "invalid" else 0.1)
                await ws.send(json.dumps(FIXTURE["game_over"]))
                if round_number == 1:
                    await ws.send(json.dumps(dict(type="lobby", tick=0)))
                    assert json.loads(await ws.recv())["type"] == "select_powerups"
                    assert json.loads(await ws.recv())["type"] == "ready"
            await ws.wait_closed()
        except ConnectionClosed:
            if mode != "timeout":
                errors.append("unexpected early client close")
        except Exception as error:
            errors.append(repr(error))

    async with serve(handler, "127.0.0.1", 0, ssl=tls) as server:
        port = server.sockets[0].getsockname()[1]
        scheme = "wss" if tls else "ws"
        recording = directory / f"{mode}-{host}-{bool(ca)}.jsonl"
        args = [CLIENT, f"{scheme}://{host}:{port}/bot", mode, str(recording)]
        if ca:
            args.append(str(ca))
        process = await asyncio.create_subprocess_exec(*args, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
        try:
            stdout, stderr = await asyncio.wait_for(process.communicate(), 12)
        except BaseException:
            process.kill()
            await process.wait()
            raise
        assert process.returncode == 0, stderr.decode()
        result = json.loads(stdout)
    assert not errors, errors
    assert result["running"] is False and result["phase"] == "disconnected", result
    assert "synthetic-cpp-test-token" not in recording.read_text(), result
    if mode in ("normal", "invalid", "retry", "tls"):
        assert result["exception"] is False and result["rounds"] == 2 and result["ticks"] == 2, result
        assert result["malformed"] == 4 and result["replayed"] == 2, result
        assert result["callback_errors"] == (2 if mode == "invalid" else 0), result
        assert result["hashes"] == ["fixture-config-1", "changed"], result
        assert result["telemetry"] and all(s["shell_speed"] == 91 for s in result["starts"]), result
    elif mode == "auth":
        assert result["rejected"] == {"unauthorized": 1} and connections == 1, result
    elif mode == "active-close":
        assert connections == 1 and result["disconnect_phase"] == "running", result
        assert result["disconnect_code"] == 1011, result
    elif mode == "protocol":
        assert result["exception"] == "protocol", result
    else:
        assert result["exception"] is True, result
    if mode == "retry":
        assert connections == 3, result
    if mode == "tls-untrusted" or mode == "tls-hostname":
        assert connections == 0, result
    print(f"PASS {mode}")


async def main():
    with tempfile.TemporaryDirectory(prefix="naval-transport-") as temporary:
        directory = Path(temporary)
        for mode in ("normal", "invalid", "retry", "auth", "active-close", "protocol", "timeout"):
            await exercise(mode, directory)
        if shutil.which("openssl"):
            cert, key = directory / "cert.pem", directory / "key.pem"
            subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                "-keyout", str(key), "-out", str(cert), "-subj", "/CN=localhost",
                "-addext", "subjectAltName=DNS:localhost"], check=True, capture_output=True)
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(cert, key)
            await exercise("tls", directory, context, cert, "localhost")
            await exercise("tls-untrusted", directory, context, None, "localhost")
            await exercise("tls-hostname", directory, context, cert, "127.0.0.1")
        else:
            print("SKIP TLS tests: openssl command not installed")


asyncio.run(main())
