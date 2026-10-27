"""PlatformIO's ELF summary omits some bytes that are present in firmware.bin."""
from pathlib import Path
import subprocess
import sys

Import('env')


def verify_image(source, target, env):
    project = Path(env.subst('$PROJECT_DIR'))
    partitions = Path(env.subst('$BUILD_DIR/partitions.bin'))
    return subprocess.call([sys.executable, str(project / 'tools/check_firmware_size.py'),
                            str(target[0]), str(partitions)])


env.AddPostAction('$BUILD_DIR/${PROGNAME}.bin', verify_image)
project = Path(env.subst('$PROJECT_DIR'))
partitions = Path(env.subst('$BUILD_DIR/partitions.bin'))
env.Depends('$BUILD_DIR/${PROGNAME}.bin', [str(partitions), str(project / 'tools/check_firmware_size.py')])
