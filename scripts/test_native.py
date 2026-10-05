#!/usr/bin/env python3
"""Run firmware core/config/TCP code on the host, with sanitizers and a QLab mock."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
json_include = root / '.pio/libdeps/frontdoor/ArduinoJson/src'
if not json_include.is_dir():
    raise SystemExit('First run: pio run -e frontdoor (installs the pinned ArduinoJson dependency).')
compiler = shutil.which('clang++') or shutil.which('g++')
if not compiler:
    raise SystemExit('Install a C++ compiler to run host tests.')
with tempfile.TemporaryDirectory(prefix='elevator-tests-') as temporary:
    binary = Path(temporary) / 'tests'
    command = [compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-g',
               '-fsanitize=address,undefined', '-DFRONT_DOOR', '-DELEVATOR_NATIVE_TEST']
    for include in ['tests/stubs', 'include', 'lib/DoorCore', str(json_include)]:
        command.extend(['-I', str(root / include)])
    command.extend(str(p) for p in sorted((root / 'lib/DoorCore').glob('*.cpp')))
    command.extend(str(root / p) for p in ['src/Mcp23008.cpp', 'src/Config.cpp', 'src/OscTcp.cpp', 'src/WebUI.cpp', 'tests/test_main.cpp'])
    command.extend(['-o', str(binary)])
    subprocess.run(command, check=True, cwd=root)
    subprocess.run([str(binary)], check=True, cwd=root)
