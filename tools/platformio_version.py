"""Refresh the identity before PlatformIO scans source dependencies."""
from pathlib import Path
import subprocess
import sys

Import('env')
root = Path(env.subst('$PROJECT_DIR'))
subprocess.run([sys.executable, str(root / 'tools/project_version.py'),
                '--output', str(Path(env.subst('$PROJECT_SRC_DIR')) / 'build_identity.hpp')], check=True)
