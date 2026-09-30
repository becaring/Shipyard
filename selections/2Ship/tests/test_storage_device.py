from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
installation = subprocess.check_output([
    str(vswhere), '-latest', '-products', '*', '-requires',
    'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
], text=True).strip()

with tempfile.TemporaryDirectory(prefix='shipyard-storage-') as folder:
    output = Path(folder)
    batch = output / 'compile.cmd'
    batch.write_text(
        '@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
        'if errorlevel 1 exit /b 1\n'
        'cl /nologo /EHsc /W4 /MT /I"' + str(ROOT / 'tests/storage-sdk') +
        '" /I"' + str(ROOT / 'sources') + '" "' +
        str(ROOT / 'tests/storage_device_contract.cpp') + '" "' +
        str(ROOT / 'sources/storage/device.cpp') + '" /Fe"' +
        str(output / 'contract.exe') + '"\nexit /b %errorlevel%\n'
    )
    built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)],
                           cwd=output, capture_output=True, text=True)
    if built.returncode:
        raise RuntimeError(built.stdout + built.stderr)
    result = subprocess.run([str(output / 'contract.exe')], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    print(result.stdout.strip())
