"""Flash firmware (.IMG) directly to Fosi DAP Neo via Rockusb.

Supports flashing both stock firmware (DP3_V137.img) and custom firmware
(DP3_V147.IMG, NeoPod, etc.) directly to flash Slot 1 & Slot 2.

No need to reinstall stock firmware first!
After reset, waits for the NeoPod SD card, then copies and verifies the matching
six .echo companions. Settings, Bluetooth records and library files are preserved.
Use a complete build package with SHA256SUMS.txt beside the image.

Usage:
    python flash-neo.py [path/to/firmware.img]
    Open flash-neo.exe for the graphical three-step updater.

Examples:
    python flash-neo.py build/NeoPod-v1.7.3/DP3_V173.IMG
    python flash-neo.py DP3_V137.img --skip-companions

Requires DAP Neo in Rockusb recovery mode:
  1. Unplug USB cable.
  2. If player is on, hold Power for 2-3 seconds until screen turns off.
  3. Hold Vol+ + Vol- + OK together (do not touch Power).
  4. Plug in USB cable while holding those 3 buttons for 3-5 seconds, then release.
  5. Screen stays black (recovery mode).
  6. Run this script!
"""
import ctypes as c
from ctypes import wintypes as w
import winreg, struct, hashlib, sys, time, argparse
import json, os, re, shutil, subprocess, tempfile
from pathlib import Path

k = c.WinDLL('kernel32', use_last_error=True)
class OV(c.Structure):
    _fields_ = [('Internal', c.c_size_t), ('InternalHigh', c.c_size_t),
                ('Offset', w.DWORD), ('OffsetHigh', w.DWORD), ('hEvent', w.HANDLE)]

k.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, c.c_void_p, w.DWORD, w.DWORD, w.HANDLE]
k.CreateFileW.restype = w.HANDLE
k.CreateEventW.argtypes = [c.c_void_p, w.BOOL, w.BOOL, w.LPCWSTR]
k.CreateEventW.restype = w.HANDLE
for name in ('ReadFile', 'WriteFile'):
    getattr(k, name).argtypes = [w.HANDLE, c.c_void_p, w.DWORD, c.POINTER(w.DWORD), c.POINTER(OV)]
    getattr(k, name).restype = w.BOOL
k.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
k.WaitForSingleObject.restype = w.DWORD
k.GetOverlappedResult.argtypes = [w.HANDLE, c.POINTER(OV), c.POINTER(w.DWORD), w.BOOL]
k.CancelIoEx.argtypes = [w.HANDLE, c.POINTER(OV)]
k.CloseHandle.argtypes = [w.HANDLE]

def device_path():
    base = r'SYSTEM\CurrentControlSet\Control\DeviceClasses\{a5dcbf10-6530-11d2-901f-00c04fb951ed}'
    found = []
    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, base) as key:
            for i in range(winreg.QueryInfoKey(key)[0]):
                name = winreg.EnumKey(key, i)
                if '#vid_2207&pid_262d#' in name.lower():
                    found.append('\\\\?\\' + name[4:])
    except Exception:
        pass
    if len(found) != 1:
        raise RuntimeError(f'Expected 1 Rockusb recovery interface, found {len(found)}. Ensure DAP Neo is in recovery mode (Vol+ + Vol- + OK while plugging in USB).')
    return found[0]

def transfer(h, data_or_length, write=False):
    length = len(data_or_length) if write else data_or_length
    buf = c.create_string_buffer(data_or_length if write else length)
    ov = OV()
    ov.hEvent = k.CreateEventW(None, True, False, None)
    if not ov.hEvent: raise c.WinError(c.get_last_error())
    count = w.DWORD()
    try:
        ok = (k.WriteFile if write else k.ReadFile)(h, buf, length, c.byref(count), c.byref(ov))
        if not ok:
            err = c.get_last_error()
            if err != 997: raise c.WinError(err)
            if k.WaitForSingleObject(ov.hEvent, 8000) != 0:
                k.CancelIoEx(h, c.byref(ov))
                k.GetOverlappedResult(h, c.byref(ov), c.byref(count), True)
                raise TimeoutError('USB transfer timed out')
            if not k.GetOverlappedResult(h, c.byref(ov), c.byref(count), False):
                raise c.WinError(c.get_last_error())
        if write and count.value != length:
            raise RuntimeError(f'Short write: {count.value} / {length}')
        return buf.raw[:count.value]
    finally:
        k.CloseHandle(ov.hEvent)

class Rockusb:
    def __init__(self, path):
        self.tag = 0x4e454f00
        self.h = k.CreateFileW(path + '\\0', 0xc0000000, 3, None, 3, 0x40000000, None)
        if self.h == c.c_void_p(-1).value: raise c.WinError(c.get_last_error())
        self.write_h = k.CreateFileW(path + '\\1', 0xc0000000, 3, None, 3, 0x40000000, None)
        if self.write_h == c.c_void_p(-1).value:
            k.CloseHandle(self.h)
            raise c.WinError(c.get_last_error())

    def close(self):
        for name in ('h', 'write_h'):
            handle = getattr(self, name, None)
            if handle is not None:
                setattr(self, name, None)
                k.CloseHandle(handle)

    def command(self, opcode, length, address=0, sectors=0):
        self.tag += 1
        cb = bytes([opcode, 0]) + struct.pack('>I', address) + b'\0' + struct.pack('>H', sectors) + b'\0'*7
        cbw = struct.pack('<IIIBBB', 0x43425355, self.tag, length, 0x80, 0, 10 if opcode == 0x14 else 6) + cb
        transfer(self.write_h, cbw, write=True)
        payload = transfer(self.h, 512 if opcode == 0x1a else length, write=False)
        status = transfer(self.h, 13, write=False)
        sign, tag, residue, result = struct.unpack('<IIIB', status)
        if sign != 0x53425355 or tag != self.tag or result != 0:
            raise RuntimeError(f'Command 0x{opcode:x} failed: status={result}')
        return payload

    def write_lba(self, lba, data):
        sectors = len(data) // 512
        self.tag += 1
        cb = bytes([0x15, 0]) + struct.pack('>I', lba) + b'\0' + struct.pack('>H', sectors) + b'\0'*7
        cbw = struct.pack('<IIIBBB', 0x43425355, self.tag, len(data), 0x00, 0, 10) + cb
        transfer(self.write_h, cbw, write=True)
        transfer(self.write_h, data, write=True)
        status = transfer(self.h, 13, write=False)
        sign, tag, residue, result = struct.unpack('<IIIB', status)
        if sign != 0x53425355 or tag != self.tag or result != 0:
            raise RuntimeError(f'Write failed at LBA {lba}: status={result}')

    def reset(self, subcode=0):
        # Rockchip reset subcodes: 0 normal, 1 MSC boot, 4 disconnect-reset.
        # Never use power-off (2) or Maskrom (3) for this workflow.
        if subcode not in (0, 1, 4):
            raise ValueError('Only normal, MSC and disconnect-reset modes are supported')
        self.tag += 1
        cb = bytes([0xFF, subcode]) + bytes(14)
        cbw = struct.pack('<IIIBBB', 0x43425355, self.tag, 0, 0x00, 0, 6) + cb
        transfer(self.write_h, cbw, write=True)
        status = transfer(self.h, 13, write=False)
        if len(status) != 13:
            raise RuntimeError(f'Short reset status: {len(status)} / 13 bytes')
        sign, tag, residue, result = struct.unpack('<IIIB', status)
        if sign != 0x53425355 or tag != self.tag or residue != 0 or result != 0:
            raise RuntimeError(f'Reset rejected or invalid status: result={result}, residue={residue}')


def recovery_interface_present(path):
    # Registry entries survive unplugging; opening the actual recovery pipe
    # checks whether the live interface is still available. No I/O is sent.
    handle = k.CreateFileW(path + '\\0', 0xc0000000, 3, None, 3, 0x40000000, None)
    if handle == c.c_void_p(-1).value:
        error = c.get_last_error()
        if error in (2, 3, 1167):
            return False
        raise c.WinError(error)
    k.CloseHandle(handle)
    return True


def wait_recovery_disconnect(path, timeout=12.0):
    deadline = time.monotonic() + timeout
    absent = 0
    while time.monotonic() < deadline:
        if recovery_interface_present(path):
            absent = 0
        else:
            absent += 1
            if absent >= 3:
                return True
        time.sleep(0.25)
    return False


def restart_player(usb, path):
    # Normal reset is confirmed on the Neo; MSC boot failed on the user's unit.
    modes = ((0, 'normal reset'), (4, 'disconnect reset'), (1, 'normal-storage boot'))
    for attempt, (subcode, description) in enumerate(modes):
        if attempt:
            try:
                usb = Rockusb(path)
            except OSError as error:
                print(f'Could not reopen recovery interface: {error}')
                return False
        print(f'Trying {description} (Rockchip reset {subcode})...')
        try:
            usb.reset(subcode)
        except (OSError, TimeoutError, RuntimeError) as error:
            # Some loaders detach before returning their status packet.
            print(f'Reset response: {error}')
        finally:
            usb.close()
        try:
            if wait_recovery_disconnect(path):
                return True
        except OSError as error:
            print(f'Could not confirm recovery disconnect: {error}')
            return False
    return False


def live_recovery_path():
    """Return a usable recovery interface, not a stale registry entry."""
    try:
        path = device_path()
        return path if recovery_interface_present(path) else None
    except (OSError, RuntimeError):
        return None


COMPANION_NAMES = ('neo_scan.bin', 'neo_flac.bin', 'neo_mp3.bin',
                   'neo_aac.bin', 'neo_opus.bin', 'boot.rgb565')


def default_image():
    # A one-file EXE extracts __file__ to a temporary directory. Firmware stays
    # beside the executable, outside that temporary bundle.
    root = Path(sys.executable if getattr(sys, 'frozen', False) else __file__).resolve().parent
    if (root / 'NeoPod' / 'release').is_dir():
        root = root / 'NeoPod'
    candidates = []
    for directory in (root / 'release', root):
        for file in directory.glob('DP3_V*.IMG'):
            match = re.fullmatch(r'DP3_V(\d+)\.IMG', file.name, re.IGNORECASE)
            if match:
                candidates.append((int(match[1]), file))
    if not candidates:
        raise FileNotFoundError('No firmware image found; supply its path.')
    return max(candidates, key=lambda item: item[0])[1].resolve()


def companion_payload(image, source=None):
    """Validate the package before any firmware writes. Never mix build versions."""
    source = Path(source).resolve() if source else image.parent / 'sd-card' / '.echo'
    manifest = image.parent / 'SHA256SUMS.txt'
    if not manifest.is_file():
        raise FileNotFoundError(f'Missing {manifest}. Use a complete build package, or --skip-companions.')
    checksums = {}
    for line in manifest.read_text(encoding='utf-8-sig').splitlines():
        if not line.strip():
            continue
        digest, name = line.split(maxsplit=1)
        name = name.lstrip('*').replace('\\', '/')
        basename = name.rsplit('/', 1)[-1]
        if not re.fullmatch(r'[0-9a-fA-F]{64}', digest) or basename in checksums:
            raise ValueError(f'Invalid or duplicate checksum entry: {line}')
        checksums[basename] = digest.lower()
    image_digest = hashlib.sha256(image.read_bytes()).hexdigest()
    expected = checksums.get(image.name)
    if image.name.upper() == 'RKNANOFW.IMG':
        valid_image = any(name.upper().endswith('.IMG') and digest == image_digest
                          for name, digest in checksums.items())
    else:
        valid_image = expected == image_digest
    if not valid_image:
        raise ValueError('Firmware does not match its SHA256SUMS.txt. Select the matching build package.')
    payload = {}
    for name in COMPANION_NAMES:
        file = source / name
        data = file.read_bytes()
        if hashlib.sha256(data).hexdigest() != checksums.get(name):
            raise ValueError(f'Companion checksum mismatch: {file}')
        payload[name] = data
    print(f'Validated firmware and {len(payload)} matching SD companion files.')
    return payload


# Require both NeoPod's USB ancestry and its SD-card LUN. An arbitrary removable
# drive or any folder named .echo is never enough to authorize automatic copying.
DRIVE_QUERY = r"""
$ErrorActionPreference = 'Stop'
$result = @()
foreach ($disk in Get-CimInstance Win32_DiskDrive) {
    if ($disk.SCSILogicalUnit -ne 1 -or $disk.Model -notmatch 'ECHOPOD.*SD card') { continue }
    $instance = $disk.PNPDeviceID
    $neo = $false
    for ($depth = 0; $depth -lt 8 -and $instance; $depth++) {
        if ($instance -match '^USB\\VID_1209&PID_0001') { $neo = $true; break }
        $instance = (Get-PnpDeviceProperty -InstanceId $instance -KeyName 'DEVPKEY_Device_Parent' -ErrorAction SilentlyContinue).Data
    }
    if (-not $neo) { continue }
    foreach ($partition in Get-CimAssociatedInstance -InputObject $disk -Association Win32_DiskDriveToDiskPartition) {
        foreach ($volume in Get-CimAssociatedInstance -InputObject $partition -Association Win32_LogicalDiskToPartition) {
            $result += [pscustomobject]@{root=($volume.DeviceID + '\'); disk=[int]$disk.Index; id=$disk.PNPDeviceID; usb=$instance}
        }
    }
}
ConvertTo-Json -InputObject @($result) -Compress
"""


def neopod_sd_drives():
    result = subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command', DRIVE_QUERY],
                            capture_output=True, text=True, timeout=15,
                            creationflags=subprocess.CREATE_NO_WINDOW)
    if result.returncode:
        raise RuntimeError('Windows disk identification failed: ' + result.stderr.strip())
    return json.loads(result.stdout.strip() or '[]')


def checked_drive_root(value):
    drive = Path(value).resolve()
    if str(drive) != drive.anchor or not drive.is_dir():
        raise ValueError('--sd-drive must name an existing drive root, for example F:\\')
    if drive.drive.lower() == os.environ.get('SystemDrive', 'C:').lower():
        raise ValueError('Refusing to use the Windows system drive as the SD card.')
    return drive


def wait_for_sd(timeout=180.0, sd_drive=None):
    print('Waiting for the NeoPod MicroSD drive to mount...')
    print('If reboot stalls, press the pinhole reset once. Keep USB connected and use USB Storage mode.')
    deadline = time.monotonic() + timeout
    last_problem = None
    while time.monotonic() < deadline:
        if sd_drive:
            try:
                return checked_drive_root(sd_drive)
            except (ValueError, OSError) as error:
                last_problem = str(error)
        else:
            try:
                candidates = neopod_sd_drives()
                if len(candidates) > 1:
                    raise ValueError('Multiple NeoPod SD volumes found. Select one explicitly with --sd-drive.')
                if len(candidates) == 1:
                    return checked_drive_root(candidates[0]['root'])
            except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
                last_problem = str(error)
        time.sleep(1)
    detail = f' Last disk check: {last_problem}' if last_problem else ''
    raise TimeoutError('The SD drive did not mount. No companion files were copied.'
                       ' After mounting it, run with --sync-only and the same image.' + detail)


def request_device_eject(instance_id):
    """Ask Windows PnP to safely remove the USB parent, covering both LUNs."""
    cfg = c.WinDLL('cfgmgr32', use_last_error=True)
    cfg.CM_Locate_DevNodeW.argtypes = [c.POINTER(w.DWORD), w.LPWSTR, w.ULONG]
    cfg.CM_Locate_DevNodeW.restype = w.ULONG
    cfg.CM_Request_Device_EjectW.argtypes = [w.DWORD, c.POINTER(w.DWORD), w.LPWSTR, w.ULONG, w.ULONG]
    cfg.CM_Request_Device_EjectW.restype = w.ULONG
    node, veto = w.DWORD(), w.DWORD()
    reason = c.create_unicode_buffer(260)
    result = cfg.CM_Locate_DevNodeW(c.byref(node), instance_id, 0)
    if result != 0:
        raise RuntimeError(f'Windows could not locate the NeoPod USB device (0x{result:x}).')
    result = cfg.CM_Request_Device_EjectW(node, c.byref(veto), reason, len(reason), 0)
    if result != 0:
        raise RuntimeError(f'Windows refused safe removal (0x{result:x}, veto {veto.value}: {reason.value}).'
                           ' Close open player files/Explorer windows and use Safely Remove Hardware.')


def eject_player(root):
    root = Path(root).resolve()
    matches = [device for device in neopod_sd_drives()
               if Path(device['root']).resolve() == root]
    if len(matches) != 1:
        raise RuntimeError('Could not uniquely identify this SD drive as the connected DAP Neo. Eject it manually.')
    instance = matches[0].get('usb', '')
    if not re.match(r'^USB\\VID_1209&PID_0001(?:[&\\])', instance, re.IGNORECASE):
        raise RuntimeError('The USB parent is not the identified NeoPod. Eject it manually.')
    request_device_eject(instance)
    print('[EJECTED] Windows accepted safe removal of DAP Neo and both storage drives. You can unplug USB.')


def prompt_to_eject(root):
    try:
        input('\nPress Enter to safely eject DAP Neo, or close this window to leave it mounted: ')
    except (EOFError, KeyboardInterrupt):
        print('\nLeft mounted. Use Windows Safely Remove Hardware before unplugging.')
        return
    try:
        eject_player(root)
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        print(f'[NOT EJECTED] {error}')


def sync_companions(root, payload):
    """Stage, back up, replace and read-verify only the six supplied companions."""
    root = Path(root).resolve()
    if set(payload) != set(COMPANION_NAMES):
        raise ValueError('Expected exactly the six firmware companion files.')
    echo = root / '.echo'
    if echo.is_symlink() or (echo.exists() and echo.resolve().parent != root):
        raise ValueError('Refusing a redirected .echo directory.')
    echo.mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='.neopod-update-', dir=echo))
    backup = work / 'backup'
    backup.mkdir()
    existed, replaced = set(), []
    try:
        for name, data in payload.items():
            destination = echo / name
            if destination.is_symlink() or destination.is_dir():
                raise ValueError(f'Refusing a redirected or non-file companion: {destination}')
            if destination.exists():
                shutil.copyfile(destination, backup / name)
                existed.add(name)
            with (work / name).open('wb') as output:
                output.write(data)
                output.flush()
                os.fsync(output.fileno())
            if (work / name).read_bytes() != data:
                raise IOError(f'Staged verification failed: {name}')
        for name, data in payload.items():
            destination = echo / name
            os.replace(work / name, destination)
            replaced.append(name)
            if destination.read_bytes() != data:
                raise IOError(f'SD read-back verification failed: {name}')
            print(f'  Updated and verified: .echo/{name}')
    except Exception:
        for name in reversed(replaced):
            try:
                if name in existed:
                    shutil.copyfile(backup / name, echo / name)
                else:
                    (echo / name).unlink(missing_ok=True)
            except OSError as error:
                print(f'Rollback needs attention for {name}: {error}')
        print(f'Update failed. Backups retained at {backup}')
        raise
    # Keep the old companion copies until the owner confirms the new build works.
    print(f'Previous companion files backed up at {backup}')
    print('[COMPLETE] Firmware companions copied and verified. Safely eject both player drives, then unplug USB.')


def checked_firmware_bytes(image_path):
    image_path = Path(image_path)
    if not image_path.exists():
        raise FileNotFoundError(f"Missing image file: {image_path}")

    raw_data = image_path.read_bytes()
    if len(raw_data) < 13631488:
        raise ValueError(f"Image too small ({len(raw_data)} bytes). Expected at least 13631488 bytes.")

    if raw_data[0x1f8:0x200] != b'RKnanoFW':
        raise ValueError(f"Invalid image magic: expected 'RKnanoFW' at offset 0x1f8, found {raw_data[0x1f8:0x200]}")
    return raw_data[:13631488]


def flash(image_path=None, *, skip_companions=False, companions_dir=None,
          sd_drive=None, mount_timeout=180.0, sync_only=False):
    image_path = Path(image_path).resolve() if image_path else default_image()
    payload = None if skip_companions else companion_payload(image_path, companions_dir)
    raw_payload = checked_firmware_bytes(image_path)
    total_sectors = len(raw_payload) // 512

    if sd_drive:
        # Validate syntax before flashing; the drive itself may mount only later.
        candidate = Path(sd_drive).resolve()
        if str(candidate) != candidate.anchor or candidate.drive.lower() == os.environ.get('SystemDrive', 'C:').lower():
            raise ValueError('--sd-drive must be a non-system drive root, for example F:\\')
    if sync_only:
        mounted = wait_for_sd(mount_timeout, sd_drive)
        sync_companions(mounted, payload)
        return mounted
    print(f"Firmware to flash: {image_path.name} ({len(raw_payload)} bytes / {total_sectors} sectors)")
    print("Locating DAP Neo in Rockusb recovery mode...")
    path = device_path()
    print(f"Connected: {path}")

    usb = Rockusb(path)
    try:
        chip = usb.command(0x1B, 16)
        print("Chip Info:", chip.hex(), repr(chip[:16]))
        flash_info = usb.command(0x1A, 11)
        print("Flash Info:", flash_info.hex())

        slots = [("Slot 1 (sector 0)", 0), ("Slot 2 (sector 30720)", 30720)]

        for slot_name, base_lba in slots:
            print(f"\nWriting {image_path.name} to {slot_name}...")
            chunk_sectors = 16
            for sec in range(0, total_sectors, chunk_sectors):
                count = min(chunk_sectors, total_sectors - sec)
                chunk = raw_payload[sec * 512:(sec + count) * 512]
                usb.write_lba(base_lba + sec, chunk)
                if sec % 2048 == 0 or sec + count >= total_sectors:
                    pct = (sec + count) * 100 // total_sectors
                    print(f"  {pct}% ({sec * 512 // 1048576} / {len(raw_payload) // 1048576} MiB)", flush=True)

        print("\nVerifying written Slot 1...")
        check = usb.command(0x14, 512, 0, 1)
        if check[:512] == raw_payload[:512]:
            print(f"[SUCCESS] Verified Slot 1 matches {image_path.name} header!")
        else:
            raise RuntimeError("Verification mismatch on Slot 1!")

        print("\nResetting DAP Neo...")
        disconnected = restart_player(usb, path)
        print(f"\n[COMPLETE] Successfully flashed {image_path.name}.")
        if disconnected:
            print('Recovery interface disconnected; waiting for normal boot and SD mount.')
        else:
            print('[NOTE] Recovery did not disconnect. Press the pinhole reset once; no reflash is needed.')
    finally:
        usb.close()
    if payload is not None:
        mounted = wait_for_sd(mount_timeout, sd_drive)
        sync_companions(mounted, payload)
        return mounted

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', nargs='?', help='Firmware image to flash')
    parser.add_argument('--reset-only', action='store_true',
                        help='Try restarting a player already in recovery; do not write firmware')
    parser.add_argument('--skip-companions', action='store_true', help='Flash firmware only; do not copy SD companions')
    parser.add_argument('--companions-dir', help='Matching companion source directory (validated against the image manifest)')
    parser.add_argument('--sd-drive', help='Explicit MicroSD drive root, e.g. F:\\; otherwise identify the NeoPod SD LUN automatically')
    parser.add_argument('--mount-timeout', type=float, default=180, help='Seconds to wait for SD mount (default: 180)')
    parser.add_argument('--sync-only', action='store_true', help='Copy matching companions after a manual reboot; do not flash or reset')
    parser.add_argument('--no-eject-prompt', action='store_true', help='Finish without waiting for Enter or ejecting the player')
    args = parser.parse_args()
    if not 0 < args.mount_timeout <= 3600:
        parser.error('--mount-timeout must be between 0 and 3600 seconds')
    if args.sync_only and (args.reset_only or args.skip_companions):
        parser.error('--sync-only cannot be combined with --reset-only or --skip-companions')
    if args.reset_only:
        if args.image:
            parser.error('--reset-only does not accept a firmware image')
        path = device_path()
        usb = Rockusb(path)
        try:
            if restart_player(usb, path):
                print('Recovery interface disconnected after reset; the player should now boot.')
            else:
                print('Automatic restart was not confirmed. Use the pinhole reset as usual.')
        finally:
            usb.close()
    else:
        try:
            mounted = flash(args.image, skip_companions=args.skip_companions,
                  companions_dir=args.companions_dir, sd_drive=args.sd_drive,
                  mount_timeout=args.mount_timeout, sync_only=args.sync_only)
            if mounted is not None and not args.no_eject_prompt:
                prompt_to_eject(mounted)
        except (OSError, RuntimeError, ValueError) as error:
            print(f'[ERROR] {error}', file=sys.stderr)
            sys.exit(1)
