#!/usr/bin/env python3
"""Exercise four browser consumers against two HUDs and a simulated controller."""
import os
import pathlib
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from test_hud_network import HudNetworkTests

HudNetworkTests.setUpClass()
try:
    rotor, _, first, second = HudNetworkTests.nodes
    for hud in (first, second):
        HudNetworkTests.request(hud, '/api/hud/discover', {'address': '127.0.0.1', 'port': rotor['ports']['hud']})
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            peers = HudNetworkTests.request(hud, '/api/hud/peers?discovered=1')[1]['items']
            if any(p['id'] == rotor['id'] for p in peers):
                break
            time.sleep(.02)
        HudNetworkTests.request(hud, '/api/hud/peers', {'id': rotor['id']})
    env = dict(os.environ, SIM_URL=f"http://127.0.0.1:{first['ports']['http']}",
               SECOND_URL=f"http://127.0.0.1:{second['ports']['http']}",
               ROTOR_URL=f"http://127.0.0.1:{rotor['ports']['http']}",
               ROTOR_TCP=str(rotor['ports']['tcp']))
    subprocess.run([env.get('NODE', 'node'), str(ROOT / 'tools/check_live_web.cjs')], env=env, check=True)
finally:
    HudNetworkTests.tearDownClass()
