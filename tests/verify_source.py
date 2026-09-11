"""Verify provenance and ensure the existing project was not modified."""
from pathlib import Path
import argparse
import difflib
import hashlib
import json
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--source-project', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
manifest = json.loads((root / 'reference/provenance.json').read_text())
sha = lambda data: hashlib.sha256(data).hexdigest()


def methods(text):
    found = {}
    for match in re.finditer(r'(?:void|bool) Logic::(\w+)\([^\{]+\{', text):
        depth, end = 1, match.end()
        while depth:
            depth += (text[end] == '{') - (text[end] == '}')
            end += 1
        found[match[1]] = text[match.start():end]
    return found


base = (root / 'reference/SCRUM16/logic.cpp').read_text()
actual = (root / 'firmware/main/source/system/logic/logic.cpp').read_text()
original_methods = methods(base)
actual_methods = methods(actual)
preserved = [name for name in original_methods if name != 'run']
parameterized = {'decideNormalAction', 'decideGradualSweepAction',
                 'decideSlowLeftAction', 'decideSlowRightAction'}
for name in preserved:
    expected = original_methods[name]
    if name in parameterized:
        expected = expected.replace('< 30.0F', '< myStopDistanceCm')
        expected = expected.replace('myPlannedAction.speed = 0.5F;', 'myPlannedAction.speed = myDriveDuty;')
        expected = expected.replace('myPlannedAction.speed = 0.2F;', 'myPlannedAction.speed = myDriveDuty;')
    assert actual_methods[name] == expected, f'Unexpected original method change: {name}'

# Original physical drivers, including the SCRUM-16 servo mapping.
for rel in ['source/driver/servo/vagrant.cpp', 'include/driver/servo/vagrant.h',
            'source/driver/pwm/esp32s3.cpp', 'source/driver/motor/mp6550.cpp',
            'source/driver/ir_sensor/esp32s3.cpp']:
    path = 'firmware/main/' + rel
    assert sha((root / path).read_bytes()) == manifest['scrum16_files'][path], path

adjusted = {'firmware/main/source/system/runtime/control.cpp',
            'firmware/main/include/system/runtime/control.h',
            'firmware/main/include/system/navigation/planner.h',
            'tools/mqtt/set-config.ps1'}
for path, digest in manifest['scrum50_files'].items():
    if path not in adjusted:
        assert sha((root / path).read_bytes()) == digest, f'MQTT copy changed: {path}'
cmake = (root / 'firmware/main/CMakeLists.txt').read_text()
assert 'system/navigation/planner.cpp' not in cmake
assert 'system/runtime/output.cpp' not in cmake
assert '.evaluate(' not in actual
assert 'Control::evaluate(' not in (root / 'firmware/main/source/system/runtime/control.cpp').read_text()
assert 'class Planner' not in (root / 'firmware/main/include/system/navigation/planner.h').read_text()

diff = ''.join(difflib.unified_diff(base.splitlines(True), actual.splitlines(True),
               fromfile='SCRUM16/logic.cpp', tofile='experiment/logic.cpp'))
(root / 'reference/logic.diff').write_text(diff, encoding='utf-8')
report = [f'{len(preserved) - len(parameterized)} original Logic methods are unchanged.',
          'Four original driving methods differ only by MQTT stop-distance and duty parameters.',
          'Motor, PWM, IR and servo implementations match SCRUM-16.',
          'Wi-Fi/MQTT drivers and protocol Manager match SCRUM-50.',
          'Operator tools match SCRUM-50 except set-config duty limit extended to 1.0.',
          'SCRUM-50 Planner, Control::evaluate and applyOutput are not part of the experiment.']
if args.source_project:
    for path, digest in manifest['source_hashes_before'].items():
        assert sha((args.source_project / path).read_bytes()) == digest, f'Original file changed: {path}'
    status = subprocess.check_output(['git', '-C', str(args.source_project),
                                      'status', '--porcelain']).decode()
    assert status == manifest['source_status_before'], 'Original Git status changed'
    report.append(f"Original project unchanged: {len(manifest['source_hashes_before'])} file hashes and Git status verified.")
print('\n'.join(report))
(root / 'reference/source-verification.txt').write_text('\n'.join(report) + '\n', encoding='utf-8')
