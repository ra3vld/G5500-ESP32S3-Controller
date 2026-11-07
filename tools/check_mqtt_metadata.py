#!/usr/bin/env python3
"""Incremental native metadata/short-target tests; --json emits the corpus."""
import argparse,fcntl,os,shlex,subprocess
from pathlib import Path
import check_native
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--json',action='store_true');args=p.parse_args()
r=Path(__file__).resolve().parents[1]
recipe=check_native.recipes(r)['mqtt_tests']
recipe=[('tests/mqtt_metadata_tests.cpp' if n=='tests/mqtt_tests.cpp' else n) for n in recipe]
recipe+=['-I','tests']
cache=r/'build/native-fast';cache.mkdir(parents=True,exist_ok=True)
with (cache/'.lock').open('a') as lock:
    fcntl.flock(lock,fcntl.LOCK_EX)
    plan,outputs=check_native.prepare_build(r,{'mqtt_metadata_tests':recipe},shlex.split(os.environ.get('CXX','g++')))
    subprocess.run(['make','-s','-j4','-f',str(plan),'selected'],cwd=r,check=True,stdout=__import__('sys').stderr)
    subprocess.run([str(outputs['mqtt_metadata_tests']),*(['--json'] if args.json else [])],cwd=r,check=True)
