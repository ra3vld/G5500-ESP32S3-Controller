"""Export actual flash offsets, evaluated capabilities and checked image parts."""
from pathlib import Path
import json
import os
import re
import shlex
import subprocess
import sys

Import('env')
root = Path(env.GetProjectOption('custom_web_flasher_root', env.subst('$PROJECT_DIR')))
sys.path.insert(0, str(root / 'tools'))
from web_flasher import ARTIFACTS, CAPABILITIES, profiles, sha256, source_digest
from project_version import resolve
from check_firmware_size import check


def export_firmware(source, target, env):
    if source_digest() != env.GetProjectOption('custom_web_flasher_source_sha256'):
        raise ValueError('Sources changed during the build; rerun this profile')
    name = env.GetProjectOption('custom_web_flasher_profile')
    profile = next(p for p in profiles() if p['id'] == name)
    build = Path(env.subst('$BUILD_DIR'))
    app = build / 'firmware.bin'
    app_size, limit = check(app, build / 'partitions.bin')
    board = env.BoardConfig()
    chip = board.get('build.mcu')
    family = {'esp32': 'ESP32', 'esp32s2': 'ESP32-S2', 'esp32s3': 'ESP32-S3', 'esp32c3': 'ESP32-C3'}[chip]
    if family != profile['chip_family']:
        raise ValueError('Profile chipset differs from the compiled target')
    flash = env.GetProjectOption('board_upload.flash_size', board.get('upload.flash_size', '4MB'))
    from build_capabilities import flash_bytes
    images = [(int(str(offset), 0), Path(env.subst(path))) for offset, path in env.get('FLASH_EXTRA_IMAGES', [])]
    images.append((int(env.subst('$ESP32_APP_OFFSET'), 0), app))
    mode = env.subst('$BOARD_FLASH_MODE')
    memory_type = env.GetProjectOption('board_build.arduino.memory_type', board.get('build.arduino.memory_type', ''))
    mode = 'dout' if memory_type in ('opi_opi', 'opi_qspi') else 'dio' if mode in ('qio', 'qout') else mode
    frequency = board.get('build.f_image', board.get('build.f_flash', '40000000L'))
    frequency = str(int(str(frequency).rstrip('Ll')) // 1000000) + 'm'
    # Official esptool patches the bootloader header and its digest. Split the
    # resulting image again so holes (especially NVS) are never written.
    merged = build / 'web-flasher-patched.bin'
    cmd = shlex.split(env.subst('$OBJCOPY')) + ['--chip', chip, 'merge-bin', '-o', str(merged),
           '--flash-mode', mode, '--flash-freq', frequency, '--flash-size', flash]
    for offset, path in images:
        cmd += [hex(offset), str(path)]
    subprocess.run(cmd, env=env['ENV'], check=True)
    merged_bytes = merged.read_bytes()
    destination = ARTIFACTS / name
    destination.mkdir(parents=True, exist_ok=True)
    files = []
    for offset, path in sorted(images):
        data = merged_bytes[offset:offset + path.stat().st_size]
        if len(data) != path.stat().st_size:
            raise ValueError('Incomplete patched flash image')
        output = destination / path.name
        output.write_bytes(data)
        files.append(dict(path=path.name, offset=offset, size=len(data), sha256=sha256(output)))
    # Construct argv directly: SCons' shell-escaped defines (notably board names
    # containing spaces) must not be split or passed with shell quote escapes.
    compiler = [str(arg) for arg in env.subst_list('$CXX')[0]]
    for definition in env.get('CPPDEFINES', []):
        if isinstance(definition, (list, tuple)):
            key, value = definition
            compiler.append('-D' + str(key) + '=' + str(value).replace('\\"', '"'))
        else:
            compiler.append('-D' + str(definition))
    compiler += ['-I' + env.subst('$PROJECT_SRC_DIR')]
    compiler += ['-I' + env.subst(str(path)) for path in env.get('CPPPATH', [])]
    definitions = '#include "build_config.hpp"\n' + '\n'.join(
        f'#if {flag}\nG5500_CAP_{flag}=1\n#else\nG5500_CAP_{flag}=0\n#endif' for flag in CAPABILITIES)
    result = subprocess.run(compiler + ['-E', '-P', '-x', 'c++', '-'], input=definitions,
                            text=True, capture_output=True, env=env['ENV'])
    if result.returncode:
        raise ValueError('Capability preprocessing failed: ' + result.stderr[-4000:])
    cpp = result.stdout
    capabilities = {flag: bool(int(value)) for flag, value in re.findall(r'G5500_CAP_(\w+)=(0|1)', cpp)}
    if set(capabilities) != set(CAPABILITIES):
        raise ValueError('Incomplete compiler capabilities')
    sizes = subprocess.check_output(shlex.split(env.subst('$SIZETOOL')) + ['-A', '-d', str(build / 'firmware.elf')],
                                    text=True, env=env['ENV'])
    sections = {section: int(size) for section, size in re.findall(r'^(\.\S+)\s+(\d+)\s+\d+', sizes, re.M)}
    dram = sum(size for section, size in sections.items() if re.match(r'^\.(?:dram\d*\.(?:data|bss)|noinit)$', section))
    iram = sum(size for section, size in sections.items() if re.match(r'^\.iram\d*\.', section))
    identity = resolve(root)
    report = dict(schema=1, profile=name, chip_family=family, flash_bytes=flash_bytes(flash),
                  identity=identity, source_sha256=source_digest(), app_bytes=app_size,
                  app_limit_bytes=limit, static_dram_bytes=dram, static_iram_bytes=iram,
                  runtime_heap_bytes=None, capabilities=capabilities, files=files)
    (destination / 'build.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Web flasher: exported {name}: {app_size}/{limit} bytes')


# A command target (instead of only a post-action) also refreshes metadata for an
# unchanged cached firmware.bin. It runs only after the image and size checks.
report = env.Command(env.subst('$BUILD_DIR/web-flasher-export'),
                     env.subst('$BUILD_DIR/${PROGNAME}.bin'), export_firmware)
env.AlwaysBuild(report)
env.Alias('buildprog', report)
