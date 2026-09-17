"""Generate deterministic reference results with the upstream Python SDK.

Usage: PYTHONPATH=/path/to/battle-sim-python-sdk python tools/generate_python_parity.py
Requires upstream commit 816fa9baba75c7294561a6f111aa2735f4bbdd01 and its dependencies.
"""
import copy
import dataclasses
import json
import random
from pathlib import Path

from naval_sdk import Welcome, WorldView
from naval_sdk.helpers import bearing_to, distance, lead_target
from naval_sdk.tactical import Evader, Gunner, Helm, Tracker

root = Path(__file__).resolve().parents[1]
fixture = json.loads((root / "tests/fixtures/protocol3.json").read_text())
specs = Welcome.from_dict(fixture["welcome"]).ship_specs
rng = random.Random(7327)
math_cases = []
for _ in range(250):
    a = [rng.uniform(-700, 700) for _ in range(2)]
    b = [rng.uniform(-700, 700) for _ in range(2)]
    velocity = [rng.uniform(-100, 100) for _ in range(2)]
    speed = rng.choice([0, 1, 50, 70, 120])
    math_cases.append(dict(a=a, b=b, velocity=velocity, speed=speed,
        distance=distance(a, b), bearing=bearing_to(a, b), aim=lead_target(a, b, velocity, speed)))

tracker, gunner, helm, evader = Tracker(specs), Gunner(specs), Helm(specs), Evader()
tactical_cases = []
for tick in range(160):
    data = copy.deepcopy(fixture["tick"])
    data["tick"] = tick
    data["self"]["pos"] = [10 if tick % 7 == 0 else 350, 680 if tick % 11 == 0 else 350]
    data["self"]["speed"] = rng.uniform(-2, 9)
    data["self"]["heading_deg"] = rng.uniform(0, 360)
    if tick % 20 in (0, 16):
        data["events"] = [{"type": "hit", "amount": 2}]
    contacts = []
    if tick < 110:
        for i in range(2):
            pos = [450 + i * 100 + tick * 0.4, 350 + i * 20]
            bearing = bearing_to(data["self"]["pos"], pos)
            active = tick % (i+3) != 1
            contacts.append(dict(id=f"c{tick}-{i}", kind="ship", pos=pos if active else [0, 0],
                bearing_deg=bearing, range=distance(data["self"]["pos"], pos) if active else None,
                confidence=0.9 if active else 0.5))
    data["contacts"] = contacts
    view = WorldView.from_dict(data)
    tracks = tracker.update(view)
    gunner.update(view)
    target_bearing = rng.uniform(-360, 720)
    shot = gunner.solve(view.me, tracks[0], view) if tracks else None
    evade = evader.update(view)
    tactical_cases.append(dict(view=data, tracks=[dataclasses.asdict(t) for t in tracks],
        target_bearing=target_bearing, steering=helm.steer_to_bearing(view.me, target_bearing),
        shot=dataclasses.asdict(shot) if shot else None,
        evade=evade.to_dict(tick, view.match_id) if evade else None))

output = dict(source_commit="816fa9baba75c7294561a6f111aa2735f4bbdd01", math=math_cases, tactical=tactical_cases)
(root / "tests/fixtures/python_parity.json").write_text(json.dumps(output, separators=(",", ":")) + "\n")
print(f"Generated {len(math_cases)} math and {len(tactical_cases)} tactical reference cases")
