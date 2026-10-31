"""Build and verify both Windows flash helpers from the same Python source.

Run on Windows using a supported 64-bit Python with requirements-flasher.txt
installed. --output selects a firmware package directory (default: release).
No USB device is accessed during building or verification.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def run(*args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=root / 'release')
    args = parser.parse_args()
    if sys.platform != 'win32':
        parser.error('Build the Windows EXE on Windows.')
    output = args.output.resolve()
    source = root / 'flash-neo.py'
    work = root / 'build' / 'flash-neo-exe'
    work.mkdir(parents=True, exist_ok=True)
    # A previous preview may still be open; never overwrite its running EXE.
    dist = Path(tempfile.mkdtemp(prefix='dist-', dir=work))
    # Give the unchanged CLI source an importable name for dependency analysis.
    shutil.copy2(source, work / 'flash_neo_backend.py')
    run(sys.executable, '-m', 'unittest', 'discover', '-s',
        str(root / 'tools'), '-p', 'test_flash_neo*.py')
    # Unrelated tools on PATH can contribute incompatible DLLs (notably ICU).
    # Resolve Windows/Qt dependencies using the build interpreter and OS only.
    build_env = os.environ.copy()
    windows = Path(os.environ.get('SystemRoot', r'C:\Windows'))
    build_env['PATH'] = os.pathsep.join(map(str, (Path(sys.executable).parent,
        Path(sys.base_prefix), Path(sys.base_prefix) / 'DLLs', windows / 'System32', windows)))
    run(sys.executable, '-m', 'PyInstaller', '--noconfirm', '--clean',
        '--onefile', '--windowed', '--noupx', '--name', 'flash-neo',
        '--paths', str(work), '--hidden-import', 'flash_neo_backend',
        '--distpath', str(dist), '--workpath', str(work / 'work'),
        '--specpath', str(work), str(root / 'flash_neo_gui.py'), env=build_env)
    exe = dist / 'flash-neo.exe'
    # Render the real frozen UI and verify external package discovery, without USB.
    with tempfile.TemporaryDirectory(prefix='flash-neo-smoke-') as directory:
        package = Path(directory) / 'package'
        package.mkdir()
        shutil.copy2(exe, package / exe.name)
        image = package / 'DP3_V999.IMG'
        image.write_bytes(b'invalid test image')
        report = Path(directory) / 'ui.json'
        run(sys.executable, str(source), '--help', stdout=subprocess.DEVNULL)
        run(str(package / exe.name), '--smoke-test', str(report), cwd=directory, timeout=90)
        result = json.loads(report.read_text(encoding='utf-8'))
        if Path(result['image']) != image or result['page_count'] != 3 or result['eject_enabled'] or not result['console_visible']:
            raise RuntimeError(f'GUI startup/discovery smoke test failed: {result}')
        shutil.copy2(report.with_suffix('.png'), work / 'gui-preview.png')
        for step in (2, 3):
            shutil.copy2(report.with_name(report.stem + f'-step{step}.png'), work / f'gui-preview-step{step}.png')
    output.mkdir(parents=True, exist_ok=True)
    for artifact in (source, root / 'flash_neo_gui.py', exe):
        destination = output / artifact.name
        if artifact.resolve() != destination:
            shutil.copy2(artifact, destination)
    # Separate helper checksums leave the firmware/companion manifest intact.
    checksums = ''.join(f'{hashlib.sha256((output / name).read_bytes()).hexdigest()}  {name}\n'
                        for name in ('flash-neo.py', 'flash_neo_gui.py', 'flash-neo.exe'))
    (output / 'FLASHER-SHA256SUMS.txt').write_text(checksums, encoding='ascii')
    print(f'Python and EXE helpers verified and packaged in {output}')


if __name__ == '__main__':
    main()
