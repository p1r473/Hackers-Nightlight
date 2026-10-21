"""Run pinned upstream pure helper tests. No sockets, SSH sessions or hardware."""
import argparse
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--cc', default='cc')
parser.add_argument('--cxx', default='c++')
parser.add_argument('--zig', help='Optional Zig executable, used as the C and C++ driver')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
unity = root / 'test/vendor/unity'
sources = sorted((root / 'third_party/lightjack-ssh/reverse-tunnel/test/native').glob('*/*.cpp'))
assert len(sources) == 9, 'Expected the nine pinned upstream test suites'
out = root / '.pio/remote-native-tests'
out.mkdir(parents=True, exist_ok=True)
obj = out / 'unity.o'
cc = [args.zig, 'cc'] if args.zig else [args.cc]
cxx = [args.zig, 'c++'] if args.zig else [args.cxx]
subprocess.run(cc + ['-c', str(unity / 'unity.c'), '-o', str(obj)], check=True)
for source in sources:
    exe = out / (source.parent.name + '.exe')
    subprocess.run(cxx + ['-std=c++17', '-DUNIT_TEST', '-I' + str(unity),
                    str(source), str(obj), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print(json.dumps({'suites_passed': len(sources), 'network_used': False}))
