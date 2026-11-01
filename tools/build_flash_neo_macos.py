"""Build and verify flash-neo.app for macOS from the shared Python source.

Run on macOS with a supported 64-bit Python, requirements-flasher.txt installed,
and libusb available (brew install libusb). --output selects the release folder
(default: release). No USB device is accessed during building or verification.

The resulting flash-neo.app is the macOS equivalent of flash-neo.exe. It embeds
libusb, so users do not need Homebrew. The app and its zipped copy must be built
on macOS; PyInstaller cannot cross-compile .app bundles from Windows.
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


def find_libusb(explicit=None):
    if explicit:
        path = Path(explicit).expanduser().resolve()
        if not path.is_file():
            raise SystemExit(f'libusb dylib not found: {path}')
        return path
    if shutil.which('brew'):
        result = subprocess.run(['brew', '--prefix', 'libusb'], capture_output=True, text=True)
        if result.returncode == 0:
            candidate = Path(result.stdout.strip()) / 'lib' / 'libusb-1.0.0.dylib'
            if candidate.is_file():
                return candidate
    for candidate in ('/opt/homebrew/opt/libusb/lib/libusb-1.0.0.dylib',
                      '/usr/local/opt/libusb/lib/libusb-1.0.0.dylib',
                      '/opt/local/lib/libusb-1.0.0.dylib',
                      '/usr/local/lib/libusb-1.0.0.dylib'):
        if Path(candidate).is_file():
            return Path(candidate)
    raise SystemExit('libusb not found. Install it with "brew install libusb",'
                     ' or pass --libusb /path/to/libusb-1.0.0.dylib.')


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=root / 'release')
    parser.add_argument('--libusb', type=Path, help='Explicit libusb-1.0.0.dylib to embed')
    parser.add_argument('--skip-tests', action='store_true', help='Skip mock unit tests')
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('Build flash-neo.app on macOS.')

    output = args.output.resolve()
    source = root / 'flash-neo.py'
    work = root / 'build' / 'flash-neo-macos'
    work.mkdir(parents=True, exist_ok=True)
    # A previous preview may still be open; never overwrite its running app.
    dist = Path(tempfile.mkdtemp(prefix='dist-', dir=work))
    libusb = find_libusb(args.libusb)

    if not args.skip_tests:
        run(sys.executable, '-m', 'unittest', 'discover', '-s',
            str(root / 'tools'), '-p', 'test_flash_neo*.py')

    # Give the unchanged CLI source an importable name for dependency analysis.
    shutil.copy2(source, work / 'flash_neo_backend.py')
    build_env = os.environ.copy()
    build_env.pop('PYTHONPATH', None)
    run(sys.executable, '-m', 'PyInstaller', '--noconfirm', '--clean',
        '--onedir', '--windowed', '--noupx', '--name', 'flash-neo',
        '--osx-bundle-identifier', 'org.neopod.flasher',
        '--paths', str(work), '--hidden-import', 'flash_neo_backend',
        '--add-binary', f'{libusb}:.',
        '--distpath', str(dist), '--workpath', str(work / 'work'),
        '--specpath', str(work), str(root / 'flash_neo_gui.py'), env=build_env)
    app = dist / 'flash-neo.app'
    executable = app / 'Contents' / 'MacOS' / 'flash-neo'
    if not executable.is_file():
        raise RuntimeError(f'PyInstaller did not produce {executable}')

    # Render the real frozen UI, verify external package discovery, and confirm
    # that the embedded libusb loads. No USB device is opened or accessed.
    with tempfile.TemporaryDirectory(prefix='flash-neo-smoke-') as directory:
        package = Path(directory) / 'package'
        package.mkdir()
        smoke_app = package / app.name
        run('/usr/bin/ditto', str(app), str(smoke_app))
        image = package / 'DP3_V999.IMG'
        image.write_bytes(b'invalid test image')
        report = Path(directory) / 'ui.json'
        smoke_env = os.environ.copy()
        smoke_env['QT_QPA_PLATFORM'] = 'offscreen'
        smoke_env.pop('PYTHONPATH', None)
        run(sys.executable, str(source), '--help', stdout=subprocess.DEVNULL,
            env=smoke_env)
        run(str(smoke_app / 'Contents' / 'MacOS' / 'flash-neo'),
            '--smoke-test', str(report), cwd=directory, timeout=120, env=smoke_env)
        result = json.loads(report.read_text(encoding='utf-8'))
        # macOS resolves /var/... to /private/var/..., so compare real paths.
        if (Path(result['image']).resolve() != image.resolve()
                or result['page_count'] != 3
                or result['eject_enabled'] or not result['console_visible']
                or not result.get('usb_backend')):
            raise RuntimeError(f'GUI startup/discovery smoke test failed: {result}')
        shutil.copy2(report.with_suffix('.png'), work / 'gui-preview.png')
        for step in (2, 3):
            shutil.copy2(report.with_name(report.stem + f'-step{step}.png'),
                         work / f'gui-preview-step{step}.png')

    output.mkdir(parents=True, exist_ok=True)
    for artifact in (source, root / 'flash_neo_gui.py'):
        shutil.copy2(artifact, output / artifact.name)
    app_target = output / 'flash-neo.app'
    if app_target.exists():
        shutil.rmtree(app_target)
    run('/usr/bin/ditto', str(app), str(app_target))
    archive = output / 'flash-neo-macos.zip'
    archive.unlink(missing_ok=True)
    # ditto preserves executable bits, symlinks and Qt framework layout.
    run('/usr/bin/ditto', '-c', '-k', '--sequesterRsrc', '--keepParent',
        str(app_target), str(archive))
    checksums = ''.join(
        f'{hashlib.sha256((output / name).read_bytes()).hexdigest()}  {name}\n'
        for name in ('flash-neo.py', 'flash_neo_gui.py', 'flash-neo-macos.zip'))
    (output / 'FLASHER-SHA256SUMS-macos.txt').write_text(checksums, encoding='ascii')
    print(f'macOS app and zip verified and packaged in {output}')
    print(f'Embedded libusb: {libusb}')


if __name__ == '__main__':
    main()
