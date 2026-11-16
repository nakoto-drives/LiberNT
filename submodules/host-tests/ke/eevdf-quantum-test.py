# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Exercise the live quantum-end selection and dequeue block
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
function = source.index('\nKiQuantumEnd(VOID)')
early = source.index('    if (!KiIsThreadQuantumExpired(Thread))', function)
opening = source.index('{', early)
depth = 1
end = opening + 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
early_block = source[early:end]
start = source.index('    if (Thread == Prcb->IdleThread)\n        return;', function)
start = source.index('    if (Prcb->NextThread == NULL)', start)
opening = source.index('{', start)
depth = 1
end = opening + 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
fixture = (pathlib.Path(__file__).parent / 'eevdf_quantum_fixture.c.in').read_text()
assert fixture.count('@BLOCK@') == 1
assert fixture.count('@EARLY@') == 1

with tempfile.TemporaryDirectory() as directory:
    directory = pathlib.Path(directory)
    generated = directory / 'test.c'
    executable = directory / 'test'
    generated.write_text(fixture.replace('@BLOCK@', source[start:end]).replace('@EARLY@', early_block))
    subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-multichar',
                    '-I', str(pathlib.Path(__file__).parent / 'eevdf_fixture'),
                    '-I', str(root / 'ntos/ke'), str(generated),
                    str(root / 'ntos/ke/eevdf_dispatch.c'),
                    str(root / 'ntos/ke/eevdf_state.c'),
                    str(root / 'ntos/ke/eevdf_domain.c'),
                    str(root / 'ntos/ke/eevdf_math.c'), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
