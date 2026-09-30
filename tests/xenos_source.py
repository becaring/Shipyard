"""Select native capabilities with the real XDK preprocessor for host fixtures."""
from functools import lru_cache
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@lru_cache(maxsize=None)
def core_source(**capabilities):
    compiler = Path(os.environ.get('XEDK',
        r'C:\Program Files (x86)\Microsoft Xbox 360 SDK')) / 'bin/win32/cl.exe'
    if not compiler.is_file():
        raise unittest.SkipTest('Xbox preprocessor required')
    source = (ROOT / 'sources/xenos/core.cpp').read_text()
    source = re.sub(r'^#include[^\n]*', '', source, flags=re.M)
    configuration = (ROOT / 'sources/xenos/configuration.h').read_text()
    definitions = ''.join('#define SHIPYARD_XENOS_' + key + ' ' + str(value) + '\n'
                          for key, value in capabilities.items())
    with tempfile.TemporaryDirectory(prefix='shipyard-capabilities-') as folder:
        path = Path(folder) / 'core.cpp'
        path.write_text(definitions + configuration + '\n' + source)
        result = subprocess.run([str(compiler), '/nologo', '/EP', '/TP', str(path)],
                                capture_output=True, text=True, check=True)
    return '\n'.join(line for line in result.stdout.splitlines() if line.strip()) + '\n'
