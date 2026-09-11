"""Compile all experiment translation units using an existing IDF SDK setup.

This does NOT link firmware and does NOT modify the source build directory.
"""
from pathlib import Path
import argparse
import ctypes
import json
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--compile-commands', required=True, type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
entries = json.loads(args.compile_commands.read_text())
template = next(e for e in entries if e['file'].replace('\\', '/').endswith('/system/logic/logic.cpp'))
split = ctypes.windll.shell32.CommandLineToArgvW
split.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
split.restype = ctypes.POINTER(ctypes.c_wchar_p)
count = ctypes.c_int()
parsed = split(template['command'], ctypes.byref(count))
command = [parsed[i] for i in range(count.value)]
ctypes.windll.kernel32.LocalFree(parsed)
base, skip = [], False
for arg in command:
    if skip:
        skip = False
        continue
    if arg in ('-o', '-MF', '-MT', '-MQ'):
        skip = True
        continue
    if arg in ('-c', '-MD', '-MMD', '-MP') or arg == template['file']:
        continue
    if arg.replace('\\', '/').endswith('/system/logic/logic.cpp'):
        continue
    if arg.startswith('-I') and arg.replace('\\', '/').endswith('/firmware/main/include'):
        arg = '-I' + str(root / 'firmware/main/include')
    base.append(arg)
base += ['-fsyntax-only', '-fdiagnostics-color=never']
work = root / 'tests/build/syntax'
work.mkdir(parents=True, exist_ok=True)
report, failed = [], False
for source in sorted((root / 'firmware/main/source').rglob('*.cpp')):
    result = subprocess.run([*base, str(source)], cwd=work, capture_output=True, text=True)
    report.append(('PASS ' if result.returncode == 0 else 'FAIL ') + str(source.relative_to(root)))
    print(report[-1], flush=True)
    if result.returncode:
        print(result.stderr[:5000])
        report.append(result.stderr[:5000])
        failed = True
(root / 'reference/esp-syntax-results.txt').write_text('\n'.join(report) + '\n', encoding='utf-8')
raise SystemExit(int(failed))
