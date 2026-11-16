# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Exercise exact matching of the experimental scheduler boot option
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
source = (root / 'ntos/ex/exinit.c').read_text()


def extract(name, result_type):
    start = source.index('\nstatic\n' + result_type + '\n' + name + '(') + 1
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


fixture = (pathlib.Path(__file__).parent / 'eevdf_option_fixture.c.in').read_text()
assert fixture.count('@FIND@') == 1 and fixture.count('@EXACT@') == 1
fixture = fixture.replace('@FIND@', extract('ExpFindLoadOption', 'PCSTR'))
fixture = fixture.replace('@EXACT@', extract('ExpHasExactLoadOption', 'BOOLEAN'))

with tempfile.TemporaryDirectory() as directory:
    directory = pathlib.Path(directory)
    generated = directory / 'test.c'
    executable = directory / 'test'
    generated.write_text(fixture)
    subprocess.run([args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-I', str(pathlib.Path(__file__).parent / 'eevdf_fixture'),
                    str(generated), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
