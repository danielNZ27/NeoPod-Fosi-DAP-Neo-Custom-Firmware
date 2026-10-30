"""Flash firmware (.IMG) directly to Fosi DAP Neo via Rockusb.

Supports flashing both stock firmware (DP3_V137.img) and custom firmware
(DP3_V147.IMG, NeoPod, etc.) directly to flash Slot 1 & Slot 2.

No need to reinstall stock firmware first!

Usage:
    python flash-neo.py [path/to/firmware.img]

Examples:
    python flash-neo.py release/DP3_V147.IMG
    python flash-neo.py DP3_V137.img

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
import winreg, struct, hashlib, sys, time
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
        k.CloseHandle(self.h)
        k.CloseHandle(self.write_h)

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

    def reset(self):
        self.tag += 1
        cb = bytes([0xFF, 0]) + b'\0'*14
        cbw = struct.pack('<IIIBBB', 0x43425355, self.tag, 0, 0x00, 0, 6) + cb
        try:
            transfer(self.write_h, cbw, write=True)
            transfer(self.h, 13, write=False)
        except Exception:
            pass

def flash(image_path=None):
    if not image_path:
        root = Path(__file__).resolve().parent
        candidates = [
            root / "release/DP3_V147.IMG",
            root / "DP3_V147.IMG",
            root / "DP3_V137.img",
        ]
        for c_path in candidates:
            if c_path.exists():
                image_path = c_path
                break
        if not image_path:
            raise FileNotFoundError("No image path provided and no default image found.")
    else:
        image_path = Path(image_path).resolve()

    if not image_path.exists():
        raise FileNotFoundError(f"Missing image file: {image_path}")

    raw_data = image_path.read_bytes()
    if len(raw_data) < 13631488:
        raise ValueError(f"Image too small ({len(raw_data)} bytes). Expected at least 13631488 bytes.")

    if raw_data[0x1f8:0x200] != b'RKnanoFW':
        raise ValueError(f"Invalid image magic: expected 'RKnanoFW' at offset 0x1f8, found {raw_data[0x1f8:0x200]}")

    raw_payload = raw_data[:13631488]
    total_sectors = len(raw_payload) // 512

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
        usb.reset()
        print(f"\n[COMPLETE] Successfully flashed {image_path.name} and restarted player!")
    finally:
        usb.close()

if __name__ == "__main__":
    target = sys.argv[1] if len(sys.argv) > 1 else None
    flash(target)
