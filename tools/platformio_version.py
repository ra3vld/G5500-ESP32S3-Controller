"""Refresh the identity before PlatformIO scans source dependencies."""
from pathlib import Path
import subprocess
import sys

Import('env')
root = Path(env.subst('$PROJECT_DIR'))
sys.path.insert(0, str(root / 'tools'))
from build_capabilities import partition_capabilities

board = env.BoardConfig()
partition_name = env.GetProjectOption('board_build.partitions', board.get('build.partitions', 'default.csv'))
partitions = root / partition_name
if not partitions.is_file():
    framework = Path(env.PioPlatform().get_package_dir('framework-arduinoespressif32'))
    partitions = framework / 'tools' / 'partitions' / partition_name
flash = env.GetProjectOption('board_upload.flash_size', board.get('upload.flash_size', '4MB'))
capabilities = partition_capabilities(partitions, flash)
defines = list(capabilities.items())
# LDF cannot reliably resolve the nested defaults in build_config.hpp. Give it
# the direct flag too, otherwise ArduinoOTA's global constructor survives even
# when the compiler removes every call. Explicit build flags still take priority.
flags = env.GetProjectOption('build_flags', [])
explicit = env.ParseFlagsExtended(flags)['CPPDEFINES'] if flags else []
names = {item if isinstance(item, str) else item[0] for item in explicit}
if not capabilities['G5500_HAS_OTA_PARTITIONS'] and 'ARDUINO_OTA_SUPPORT' not in names:
    defines.append(('ARDUINO_OTA_SUPPORT', 0))
env.Append(CPPDEFINES=defines)

subprocess.run([sys.executable, str(root / 'tools/project_version.py'),
                '--output', str(Path(env.subst('$PROJECT_SRC_DIR')) / 'build_identity.hpp')], check=True)
