#!/usr/bin/env python3
"""Build and run selected native tests incrementally, using the full runner's recipes."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]


def recipes(root):
    """Read literal compiler commands; leave shell loops to the full runner."""
    result = {}
    pending = [root / 'tools/build_native.sh']
    seen = set()
    while pending:
        script = pending.pop(0)
        if script in seen:
            continue
        seen.add(script)
        variables = {}
        for line in script.read_text().replace('\\\n', ' ').splitlines():
            # Only top-level constant assignments: never interpret shell code.
            assignment = re.fullmatch(r'([A-Z_]+)="([^"]*)"', line)
            if assignment:
                variables[assignment[1]] = assignment[2]
                continue
            helper = re.fullmatch(r'sh (tools/[\w_]+\.sh)', line.strip())
            if helper:
                pending.append(root / helper[1])
            if not line.strip().startswith('${CXX:-g++} '):
                continue
            command = line.strip().removeprefix('${CXX:-g++} ')
            for _ in range(len(variables) + 1):
                command = re.sub(r'\$([A-Z_]+)\b',
                                 lambda m: variables.get(m[1], m[0]), command)
            if '$' in command:
                continue
            arguments = shlex.split(command)
            if '-o' not in arguments or '-c' in arguments:
                continue
            output_index = arguments.index('-o')
            output = arguments[output_index + 1]
            del arguments[output_index:output_index + 2]
            # The simulator can be built with --build-only for browser checks.
            sources = [arg for arg in arguments if arg.endswith('.cpp')]
            if not sources:
                continue
            name = Path(output).name
            if name in result and result[name] != arguments:
                raise ValueError(f'Conflicting native recipes: {name}')
            result[name] = arguments
    return result


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest()[:20]


def make_path(path):
    return str(path).replace('$', '$$').replace('#', '\\#').replace(' ', '\\ ')


def shell_command(arguments):
    return shlex.join(map(str, arguments)).replace('$', '$$')


def prepare_build(root, selected, compiler):
    """Generate explicit Make rules with compiler dependencies and distinct flag sets."""
    cache = root / 'build/native-fast'
    version = subprocess.check_output([*compiler, '--version'], text=True)
    executables = []
    for part in compiler:
        executable = shutil.which(part)
        if executable:
            path = Path(executable).resolve()
            executables.append((str(path), path.stat().st_mtime_ns))
    identity = [compiler, version, executables]
    rules = ['.DELETE_ON_ERROR:', '.PHONY: selected', '']
    dependencies = set()
    objects = {}
    outputs = {}

    def compile_object(source, flags):
        key = digest([identity, flags])
        source = root / source
        obj = cache / 'objects' / key / source.relative_to(root).with_suffix('.o')
        if obj not in objects:
            obj.parent.mkdir(parents=True, exist_ok=True)
            dep = obj.with_suffix('.d')
            rules.extend([
                f'{make_path(obj)}: {make_path(source)}',
                '\t@echo CXX ' + shlex.quote(str(source.relative_to(root))),
                '\t@' + shell_command([*compiler, *flags, '-MMD', '-MP', '-MF', dep,
                                       '-MQ', obj, '-c', source, '-o', obj]), '',
            ])
            dependencies.add(dep)
            objects[obj] = source
        return obj

    library = None

    def ruckig():
        nonlocal library
        if library is None:
            flags = ['-std=c++17', '-O2', '-Wall', '-Wextra',
                     '-Wno-unused-parameter', '-Wno-array-bounds']
            members = [compile_object(str(source.relative_to(root)), flags)
                       for source in sorted((root / 'G5500_Fork/src/ruckig').glob('*.cpp'))]
            library = cache / 'lib' / digest([identity, flags]) / 'libruckig.a'
            library.parent.mkdir(parents=True, exist_ok=True)
            rules.extend([
                f'{make_path(library)}: ' + ' '.join(map(make_path, members)),
                '\t@echo AR libruckig.a',
                '\t@' + shell_command([*shlex.split(os.environ.get('AR', 'ar')),
                                       'rcs', library, *members]), '',
            ])
        return library

    for name, arguments in selected.items():
        flags = [arg for arg in arguments
                 if not arg.endswith(('.cpp', '.a'))]
        prerequisites = []
        for arg in arguments:
            if arg.endswith('.cpp'):
                prerequisites.append(compile_object(arg, flags))
            elif arg == 'build/motion/libruckig.a':
                prerequisites.append(ruckig())
            elif arg.endswith('.a'):
                prerequisites.append(root / arg)
        binary = cache / 'bin' / digest([identity, arguments]) / name
        binary.parent.mkdir(parents=True, exist_ok=True)
        rules.extend([
            f'{make_path(binary)}: ' + ' '.join(map(make_path, prerequisites)),
            '\t@echo LINK ' + shlex.quote(name),
            '\t@' + shell_command([*compiler, *flags, *prerequisites, '-o', binary]), '',
        ])
        outputs[name] = binary

    rules.append('selected: ' + ' '.join(map(make_path, outputs.values())))
    rules.append('-include ' + ' '.join(map(make_path, sorted(dependencies))))
    content = '\n'.join(rules) + '\n'
    plan = cache / 'plans' / (digest(content) + '.mk')
    plan.parent.mkdir(parents=True, exist_ok=True)
    if not plan.exists():
        plan.write_text(content)
    return plan, outputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tests', nargs='*', help='test names or tests/*.cpp paths')
    parser.add_argument('--list', action='store_true', help='list individual targets')
    parser.add_argument('--build-only', action='store_true', help='build without executing')
    parser.add_argument('-j', '--jobs', type=int, default=4, help='compiler jobs (default: 4)')
    args = parser.parse_args()
    available = recipes(ROOT)
    if args.list:
        print('\n'.join(sorted(available)))
        return 0
    if not args.tests or args.jobs < 1:
        parser.error('select one or more tests; jobs must be positive (see --list)')
    names = list(dict.fromkeys(Path(name).stem for name in args.tests))
    for name in names:
        if name not in available:
            parser.error(f'unknown individual target: {name} (see --list; matrix loops use build_native.sh)')
    if 'simulator' in names and not args.build_only:
        parser.error('simulator requires --build-only; start it separately for browser checks')
    started = time.monotonic()
    if (ROOT / 'G5500_Fork/project_version.hpp').exists():
        subprocess.run([sys.executable, str(ROOT / 'tools/project_version.py')], check=True)
    cache = ROOT / 'build/native-fast'
    cache.mkdir(parents=True, exist_ok=True)
    # Keep concurrent invocations from overwriting shared objects or test binaries.
    with (cache / '.lock').open('w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        compiler = shlex.split(os.environ.get('CXX', 'g++'))
        plan, outputs = prepare_build(ROOT, {name: available[name] for name in names}, compiler)
        subprocess.run(['make', '--no-print-directory', '-f', str(plan),
                        f'-j{args.jobs}', 'selected'], cwd=ROOT, check=True)
        for name, binary in outputs.items():
            if not args.build_only:
                test_started = time.monotonic()
                subprocess.run([str(binary)], cwd=ROOT, check=True)
                print(f'{name}: {time.monotonic() - test_started:.3f}s', flush=True)
            else:
                print(binary, flush=True)
    print(f'Native check: {time.monotonic() - started:.3f}s total', flush=True)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f'Native check failed: {error}', file=sys.stderr)
        sys.exit(1)
