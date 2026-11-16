# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Exercise i386's opt-in running-thread handoff
# COPYRIGHT:   Copyright 2026 LiberNT contributors

import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--root', required=True)
parser.add_argument('--cc', required=True)
args = parser.parse_args()
root = pathlib.Path(args.root)
source = (root / 'ntos/include/internal/ke_x.h').read_text()


def extract(name, result):
    start = source.index('\nFORCEINLINE\n' + result + '\n' + name + '(') + 1
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


fixture = (pathlib.Path(__file__).parent / 'eevdf_i386_handoff_fixture.c.in').read_text()
for marker, name, result in (('@WAIT@', 'KiWaitForThreadSwapOut', 'VOID'),
                             ('@LEAVING@', 'KiThreadLeavingProcessor', 'BOOLEAN'),
                             ('@DEFER@', 'KiDeferReadyWhileLeavingProcessor', 'BOOLEAN'),
                             ('@COMPLETE@', 'KiCompleteThreadSwitch', 'VOID')):
    assert fixture.count(marker) == 1
    fixture = fixture.replace(marker, extract(name, result))

with tempfile.TemporaryDirectory() as directory:
    directory = pathlib.Path(directory)
    generated = directory / 'test.c'
    executable = directory / 'test'
    generated.write_text(fixture)
    subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-I', str(pathlib.Path(__file__).parent / 'eevdf_fixture'),
                    str(generated), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
