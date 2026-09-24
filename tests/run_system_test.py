"""Build and execute main.cpp system-loop integration tests with fake I/O."""
from pathlib import Path
import argparse
import os
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--zig', required=True, type=Path)
parser.add_argument('--cjson-dir', required=True, type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
build = root / 'tests/build'
build.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(build / 'global-cache'),
           ZIG_LOCAL_CACHE_DIR=str(build / 'local-cache'))
subprocess.run([str(args.zig), 'cc', '-c', str(args.cjson_dir / 'cJSON.c'),
                '-o', str(build / 'cjson.obj')], env=env, check=True)
source = root / 'firmware/main/source'
files = ['system/communication/manager.cpp', 'system/runtime/control.cpp',
         'driver/motor/mp6550.cpp',
         'driver/servo/vagrant.cpp', 'driver/serial/stub.cpp']
cmd = [str(args.zig), 'c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
       # The original SCRUM-16 file contains unused old constants. Preserve them.
       '-Wno-unused-const-variable', '-Wno-c99-designator',
       '-I' + str(root / 'tests/include'), '-I' + str(root / 'firmware/main/include'),
       '-I' + str(args.cjson_dir), str(root / 'tests/system_test.cpp'),
       *[str(source / p) for p in files], str(build / 'cjson.obj'),
       '-o', str(build / 'system_test.exe')]
subprocess.run(cmd, env=env, check=True)
subprocess.run([str(build / 'system_test.exe')], env=env, check=True)
