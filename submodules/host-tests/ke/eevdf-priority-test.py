# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Exercise EEVDF placement through standby priority changes
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
source = (root / 'ntos/ke/schedule.c').read_text()
start = source.index('\nVOID\nFASTCALL\nKiSetPriorityThread(') + 1
opening = source.index('{', start)
depth = 1
end = opening + 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
fixture = (pathlib.Path(__file__).parent / 'eevdf_priority_fixture.c.in').read_text()
assert fixture.count('@FUNCTION@') == 1

with tempfile.TemporaryDirectory() as directory:
    directory = pathlib.Path(directory)
    generated = directory / 'test.c'
    executable = directory / 'test'
    generated.write_text(fixture.replace('@FUNCTION@', source[start:end]))
    subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-multichar',
                    '-I', str(pathlib.Path(__file__).parent / 'eevdf_fixture'),
                    '-I', str(root / 'ntos/ke'), str(generated),
                    str(root / 'ntos/ke/eevdf_state.c'),
                    str(root / 'ntos/ke/eevdf_domain.c'),
                    str(root / 'ntos/ke/eevdf_math.c'), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
