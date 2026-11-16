# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Exercise clock-triggered EEVDF reviews
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
source = (root / 'ntos/ke/clockint.c').read_text()
start = source.index('    if (KiGetQuantumUnitCycles() == 0)')
end = source.index('    KiRequestDispatchInterrupt(Prcb);', start)
end += len('    KiRequestDispatchInterrupt(Prcb);')
fixture = (pathlib.Path(__file__).parent / 'eevdf_clock_fixture.c.in').read_text()
assert fixture.count('@BLOCK@') == 1

with tempfile.TemporaryDirectory() as directory:
    directory = pathlib.Path(directory)
    generated = directory / 'test.c'
    executable = directory / 'test'
    generated.write_text(fixture.replace('@BLOCK@', source[start:end]))
    subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-I', str(pathlib.Path(__file__).parent / 'eevdf_fixture'),
                    str(generated), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
