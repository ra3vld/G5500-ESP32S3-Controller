#!/usr/bin/env python3
"""Run the HUD browser against four isolated native simulator processes."""
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from test_hud_network import HudNetworkTests

HudNetworkTests.setUpClass()
try:
    r1, r2, h1, _ = HudNetworkTests.nodes
    env = dict(os.environ, SIM_URL=f"http://127.0.0.1:{h1['ports']['http']}",
               ROTOR_URL=f"http://127.0.0.1:{r1['ports']['http']}",
               ROTOR_TWO_URL=f"http://127.0.0.1:{r2['ports']['http']}",
               ROTOR_PORT=str(r1['ports']['hud']), ROTOR_TWO_PORT=str(r2['ports']['hud']))
    subprocess.run([env.get('NODE', 'node'), str(ROOT / 'tools/check_hud_web.cjs')], env=env, check=True)
finally:
    HudNetworkTests.tearDownClass()
