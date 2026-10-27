"""Host (x64) MSVC builds for offline generators and differential harnesses.

Commands run in one batch after vcvars64.bat, so cl.exe and its environment
come from the newest Visual Studio with the x64 C++ tools.
"""

import os
from pathlib import Path
import subprocess


def vcvars():
    vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    installation = subprocess.check_output([
        str(vswhere), '-latest', '-products', '*', '-requires',
        'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
    ], text=True).strip()
    path = Path(installation) / 'VC/Auxiliary/Build/vcvars64.bat'
    if not path.is_file():
        raise FileNotFoundError('Host x64 MSVC toolchain is required: ' + str(path))
    return path


def quote(argument):
    text = str(argument)
    return '"' + text + '"' if any(c in text for c in ' &()^') and not text.startswith('"') else text


def run(commands, folder, name='build'):
    """Run argument lists in order with the host toolchain; raises with the log on failure."""
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    lines = ['@echo off', 'call "' + str(vcvars()) + '" >nul', 'if errorlevel 1 exit /b 1']
    for command in commands:
        lines += [' '.join(quote(a) for a in command), 'if errorlevel 1 exit /b 1']
    batch = folder / (name + '.cmd')
    batch.write_text('\r\n'.join(lines) + '\r\nexit /b 0\r\n', encoding='utf-8')
    result = subprocess.run([os.environ.get('ComSpec', 'cmd.exe'), '/d', '/c', str(batch)], cwd=folder,
                            capture_output=True, text=True, errors='replace')
    log = result.stdout + result.stderr
    (folder / (name + '.log')).write_text(log, encoding='utf-8')
    if result.returncode:
        raise RuntimeError(name + ' failed in ' + str(folder) + ':\n' + log[-8000:])
    return log
