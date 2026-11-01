"""Verify and package the v1.8.2 production release, including both flash tools."""
import hashlib
import importlib.util
from pathlib import Path
import shutil
import zipfile


def main():
    root = Path(__file__).resolve().parents[1]
    release = root / 'release'
    package = root / 'build' / 'NeoPod-v1.8.2'
    spec = importlib.util.spec_from_file_location('flash_neo', root / 'flash-neo.py')
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    image = release / 'DP3_V182.IMG'
    helper.checked_firmware_bytes(image)
    helper.companion_payload(image)
    files = ['DP3_V182.IMG', 'SHA256SUMS.txt', 'FLASHER-SHA256SUMS.txt',
             'flash-neo.py', 'flash_neo_gui.py', 'flash-neo.exe']
    files += ['sd-card/.echo/' + name for name in helper.COMPANION_NAMES]
    for manifest in ('SHA256SUMS.txt', 'FLASHER-SHA256SUMS.txt'):
        for line in (release / manifest).read_text(encoding='ascii').splitlines():
            digest, name = line.split('  ', 1)
            if hashlib.sha256((release / name).read_bytes()).hexdigest() != digest:
                raise ValueError(f'Package checksum mismatch: {name}')
    for name in files:
        target = package / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(release / name, target)
    shutil.copy2(release / 'RELEASE-v1.8.2.md', package / 'README.md')
    # Include licenses and the GUI sources so the bundled open-source app can
    # be inspected and rebuilt. Firmware companion paths retain their dot folder.
    shutil.copy2(root / 'LICENSE', package / 'LICENSE')
    extras = {
        'THIRD-PARTY-NOTICES.txt': 'licenses/FLASHER-THIRD-PARTY.txt',
        'licenses/LGPL-3.0.txt': 'licenses/LGPL-3.0.txt',
        'licenses/Python-LICENSE.txt': 'licenses/Python-LICENSE.txt',
        'tools/build_flash_neo.py': 'tools/build_flash_neo.py',
        'tools/requirements-flasher.txt': 'tools/requirements-flasher.txt',
    }
    for name, source in extras.items():
        target = package / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(root / source, target)
    archive = release / 'NeoPod-v1.8.2.zip'
    expected = set(files + ['README.md', 'LICENSE'] + list(extras))
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as bundle:
        for name in sorted(expected):
            bundle.write(package / name, name)
    with zipfile.ZipFile(archive) as bundle:
        if set(bundle.namelist()) != expected or bundle.testzip() is not None:
            raise ValueError('Production ZIP verification failed')
        for name in helper.COMPANION_NAMES:
            if bundle.read('sd-card/.echo/' + name) != (package / 'sd-card/.echo' / name).read_bytes():
                raise ValueError(f'ZIP companion mismatch: {name}')
    print(f'Production package verified: {package}')
    print(f'Production ZIP verified: {archive}')


if __name__ == '__main__':
    main()
