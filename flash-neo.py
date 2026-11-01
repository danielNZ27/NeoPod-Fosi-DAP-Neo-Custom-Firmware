"""Flash firmware (.IMG) directly to Fosi DAP Neo via Rockusb.

Supports Windows and macOS. Flashes both stock firmware (DP3_V137.img) and
custom firmware (DP3_V147.IMG, NeoPod, etc.) directly to flash Slot 1 & Slot 2.

No need to reinstall stock firmware first!
After reset, waits for the NeoPod SD card, then copies and verifies the matching
six .echo companions. Settings, Bluetooth records and library files are preserved.
Use a complete build package with SHA256SUMS.txt beside the image.

Usage:
    python flash-neo.py [path/to/firmware.img]       (Windows)
    python3 flash-neo.py [path/to/firmware.img]      (macOS)
    Open flash-neo.exe or flash-neo.app for the graphical three-step updater.

Examples:
    python flash-neo.py build/NeoPod-v1.7.3/DP3_V173.IMG
    python flash-neo.py DP3_V137.img --skip-companions
    python3 flash-neo.py DP3_V182.IMG --sd-drive /Volumes/NeoPod

Requires DAP Neo in Rockusb recovery mode:
  1. Unplug USB cable.
  2. If player is on, hold Power for 2-3 seconds until screen turns off.
  3. Hold Vol+ + Vol- + OK together (do not touch Power).
  4. Plug in USB cable while holding those 3 buttons for 3-5 seconds, then release.
  5. Screen stays black (recovery mode).
  6. Run this script!

The macOS Python helper needs libusb-1.0 (brew install libusb). The packaged
flash-neo.app includes its own copy. No vendor USB driver is required.
"""
import argparse
import atexit
import ctypes as c
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time

IS_WINDOWS = os.name == 'nt'
IS_MACOS = sys.platform == 'darwin'

if IS_WINDOWS:
    from ctypes import wintypes as w
    import winreg

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


CBW_SIGNATURE = 0x43425355
CSW_SIGNATURE = 0x53425355
ROCKUSB_VID = 0x2207
ROCKUSB_PID = 0x262d
NEOPOD_VID = 0x1209
NEOPOD_PID = 0x0001


def command_block(opcode, address=0, sectors=0, extra=0):
    return (bytes([opcode, extra]) + struct.pack('>I', address) + b'\0'
            + struct.pack('>H', sectors) + b'\0' * 7)


def command_packet(tag, opcode, length, address=0, sectors=0, extra=0):
    cb = command_block(opcode, address, sectors, extra)
    cb_length = 10 if opcode == 0x14 else 6
    return struct.pack('<IIIBBB', CBW_SIGNATURE, tag, length, 0x80, 0, cb_length) + cb


def write_packet(tag, lba, data):
    cb = command_block(0x15, lba, len(data) // 512)
    return struct.pack('<IIIBBB', CBW_SIGNATURE, tag, len(data), 0x00, 0, 10) + cb


def reset_packet(tag, subcode):
    cb = bytes([0xFF, subcode]) + bytes(14)
    return struct.pack('<IIIBBB', CBW_SIGNATURE, tag, 0, 0x00, 0, 6) + cb


class RockusbProtocol:
    """Shared Rockchip CBW/CSW protocol; transports only move raw bytes."""

    def __init__(self):
        self.tag = 0x4e454f00

    def _write(self, data):
        raise NotImplementedError

    def _read(self, length):
        raise NotImplementedError

    def close(self):
        raise NotImplementedError

    def command(self, opcode, length, address=0, sectors=0):
        self.tag += 1
        self._write(command_packet(self.tag, opcode, length, address, sectors))
        payload = self._read(512 if opcode == 0x1a else length)
        status = self._read(13)
        sign, tag, residue, result = struct.unpack('<IIIB', status)
        if sign != CSW_SIGNATURE or tag != self.tag or result != 0:
            raise RuntimeError(f'Command 0x{opcode:x} failed: status={result}')
        return payload

    def write_lba(self, lba, data):
        self.tag += 1
        self._write(write_packet(self.tag, lba, data))
        self._write(data)
        status = self._read(13)
        sign, tag, residue, result = struct.unpack('<IIIB', status)
        if sign != CSW_SIGNATURE or tag != self.tag or result != 0:
            raise RuntimeError(f'Write failed at LBA {lba}: status={result}')

    def reset(self, subcode=0):
        # Rockchip reset subcodes: 0 normal, 1 MSC boot, 4 disconnect-reset.
        # Never use power-off (2) or Maskrom (3) for this workflow.
        if subcode not in (0, 1, 4):
            raise ValueError('Only normal, MSC and disconnect-reset modes are supported')
        self.tag += 1
        self._write(reset_packet(self.tag, subcode))
        status = self._read(13)
        if len(status) != 13:
            raise RuntimeError(f'Short reset status: {len(status)} / 13 bytes')
        sign, tag, residue, result = struct.unpack('<IIIB', status)
        if sign != CSW_SIGNATURE or tag != self.tag or residue != 0 or result != 0:
            raise RuntimeError(f'Reset rejected or invalid status: result={result}, residue={residue}')


if IS_WINDOWS:
    def transfer(h, data_or_length, write=False):
        length = len(data_or_length) if write else data_or_length
        buf = c.create_string_buffer(data_or_length if write else length)
        ov = OV()
        ov.hEvent = k.CreateEventW(None, True, False, None)
        if not ov.hEvent:
            raise c.WinError(c.get_last_error())
        count = w.DWORD()
        try:
            ok = (k.WriteFile if write else k.ReadFile)(h, buf, length, c.byref(count), c.byref(ov))
            if not ok:
                err = c.get_last_error()
                if err != 997:
                    raise c.WinError(err)
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


    class RockusbWindows(RockusbProtocol):
        def __init__(self, path):
            super().__init__()
            self.h = k.CreateFileW(path + '\\0', 0xc0000000, 3, None, 3, 0x40000000, None)
            if self.h == c.c_void_p(-1).value:
                raise c.WinError(c.get_last_error())
            self.write_h = k.CreateFileW(path + '\\1', 0xc0000000, 3, None, 3, 0x40000000, None)
            if self.write_h == c.c_void_p(-1).value:
                k.CloseHandle(self.h)
                raise c.WinError(c.get_last_error())

        def _read(self, length):
            return transfer(self.h, length, write=False)

        def _write(self, data):
            transfer(self.write_h, data, write=True)

        def close(self):
            for name in ('h', 'write_h'):
                handle = getattr(self, name, None)
                if handle is not None:
                    setattr(self, name, None)
                    k.CloseHandle(handle)


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


# macOS talks to the Rockchip vendor interface directly through libusb. The
# recovery firmware exposes one 0xFF/6/5 interface with bulk IN and OUT pipes,
# the same endpoints rkdeveloptool uses. No kernel driver or kext is needed.
if IS_MACOS:
    class LibusbDeviceDescriptor(c.Structure):
        _fields_ = [
            ('bLength', c.c_uint8), ('bDescriptorType', c.c_uint8),
            ('bcdUSB', c.c_uint16), ('bDeviceClass', c.c_uint8),
            ('bDeviceSubClass', c.c_uint8), ('bDeviceProtocol', c.c_uint8),
            ('bMaxPacketSize0', c.c_uint8), ('idVendor', c.c_uint16),
            ('idProduct', c.c_uint16), ('bcdDevice', c.c_uint16),
            ('iManufacturer', c.c_uint8), ('iProduct', c.c_uint8),
            ('iSerialNumber', c.c_uint8), ('bNumConfigurations', c.c_uint8),
        ]

    class LibusbEndpointDescriptor(c.Structure):
        _fields_ = [
            ('bLength', c.c_uint8), ('bDescriptorType', c.c_uint8),
            ('bEndpointAddress', c.c_uint8), ('bmAttributes', c.c_uint8),
            ('wMaxPacketSize', c.c_uint16), ('bInterval', c.c_uint8),
            ('bRefresh', c.c_uint8), ('bSynchAddress', c.c_uint8),
            ('extra', c.POINTER(c.c_ubyte)), ('extra_length', c.c_int),
        ]

    class LibusbInterfaceDescriptor(c.Structure):
        _fields_ = [
            ('bLength', c.c_uint8), ('bDescriptorType', c.c_uint8),
            ('bInterfaceNumber', c.c_uint8), ('bAlternateSetting', c.c_uint8),
            ('bNumEndpoints', c.c_uint8), ('bInterfaceClass', c.c_uint8),
            ('bInterfaceSubClass', c.c_uint8), ('bInterfaceProtocol', c.c_uint8),
            ('iInterface', c.c_uint8),
            ('endpoint', c.POINTER(LibusbEndpointDescriptor)),
            ('extra', c.POINTER(c.c_ubyte)), ('extra_length', c.c_int),
        ]

    class LibusbInterface(c.Structure):
        _fields_ = [('altsetting', c.POINTER(LibusbInterfaceDescriptor)),
                    ('num_altsetting', c.c_int)]

    class LibusbConfigDescriptor(c.Structure):
        _fields_ = [
            ('bLength', c.c_uint8), ('bDescriptorType', c.c_uint8),
            ('wTotalLength', c.c_uint16), ('bNumInterfaces', c.c_uint8),
            ('bConfigurationValue', c.c_uint8), ('iConfiguration', c.c_uint8),
            ('bmAttributes', c.c_uint8), ('MaxPower', c.c_uint8),
            ('interface', c.POINTER(LibusbInterface)),
            ('extra', c.POINTER(c.c_ubyte)), ('extra_length', c.c_int),
        ]

    class MacRecoveryPath:
        """Stable bus/port address for the live recovery device."""

        __slots__ = ('bus', 'ports')

        def __init__(self, bus, ports):
            self.bus, self.ports = bus, tuple(ports)

        def __str__(self):
            ports = '.'.join(str(port) for port in self.ports) or '?'
            return f'USB bus {self.bus}, port {ports}'

        def __eq__(self, other):
            return isinstance(other, MacRecoveryPath) and (self.bus, self.ports) == (other.bus, other.ports)

        def __hash__(self):
            return hash((self.bus, self.ports))

    _libusb = None
    _libusb_initialized = False

    def _libusb_candidates():
        override = os.environ.get('NEOPOD_LIBUSB')
        if override:
            yield override
        roots = []
        frozen_root = getattr(sys, '_MEIPASS', None)
        if frozen_root:
            roots.append(Path(frozen_root))
        executable = Path(sys.executable).resolve()
        if executable.parent.name == 'MacOS' and executable.parent.parent.name == 'Contents':
            roots.append(executable.parent.parent / 'Frameworks')
        for root in roots:
            for name in ('libusb-1.0.0.dylib', 'libusb-1.0.dylib'):
                yield str(root / name)
        for directory in ('/opt/homebrew/opt/libusb/lib', '/usr/local/opt/libusb/lib',
                          '/opt/local/lib', '/usr/local/lib', '/usr/lib'):
            yield str(Path(directory) / 'libusb-1.0.0.dylib')
        from ctypes import util as ctypes_util
        found = ctypes_util.find_library('usb-1.0')
        if found:
            yield found

    def _configure_libusb(lib):
        lib.libusb_init.argtypes = [c.POINTER(c.c_void_p)]
        lib.libusb_init.restype = c.c_int
        lib.libusb_exit.argtypes = [c.c_void_p]
        lib.libusb_exit.restype = None
        lib.libusb_get_device_list.argtypes = [c.c_void_p, c.POINTER(c.POINTER(c.c_void_p))]
        lib.libusb_get_device_list.restype = c.c_ssize_t
        lib.libusb_free_device_list.argtypes = [c.POINTER(c.c_void_p), c.c_int]
        lib.libusb_free_device_list.restype = None
        lib.libusb_get_device_descriptor.argtypes = [c.c_void_p, c.POINTER(LibusbDeviceDescriptor)]
        lib.libusb_get_device_descriptor.restype = c.c_int
        lib.libusb_open.argtypes = [c.c_void_p, c.POINTER(c.c_void_p)]
        lib.libusb_open.restype = c.c_int
        lib.libusb_close.argtypes = [c.c_void_p]
        lib.libusb_close.restype = None
        lib.libusb_get_active_config_descriptor.argtypes = [
            c.c_void_p, c.POINTER(c.POINTER(LibusbConfigDescriptor))]
        lib.libusb_get_active_config_descriptor.restype = c.c_int
        lib.libusb_get_config_descriptor.argtypes = [
            c.c_void_p, c.c_uint8, c.POINTER(c.POINTER(LibusbConfigDescriptor))]
        lib.libusb_get_config_descriptor.restype = c.c_int
        lib.libusb_free_config_descriptor.argtypes = [c.POINTER(LibusbConfigDescriptor)]
        lib.libusb_free_config_descriptor.restype = None
        lib.libusb_claim_interface.argtypes = [c.c_void_p, c.c_int]
        lib.libusb_claim_interface.restype = c.c_int
        lib.libusb_release_interface.argtypes = [c.c_void_p, c.c_int]
        lib.libusb_release_interface.restype = c.c_int
        lib.libusb_bulk_transfer.argtypes = [
            c.c_void_p, c.c_ubyte, c.c_void_p, c.c_int, c.POINTER(c.c_int), c.c_uint]
        lib.libusb_bulk_transfer.restype = c.c_int
        lib.libusb_get_bus_number.argtypes = [c.c_void_p]
        lib.libusb_get_bus_number.restype = c.c_uint8
        lib.libusb_get_port_numbers.argtypes = [c.c_void_p, c.POINTER(c.c_uint8), c.c_int]
        lib.libusb_get_port_numbers.restype = c.c_int
        lib.libusb_error_name.argtypes = [c.c_int]
        lib.libusb_error_name.restype = c.c_char_p

    def _libusb_handle():
        global _libusb, _libusb_initialized
        if _libusb is None:
            errors = []
            for candidate in _libusb_candidates():
                try:
                    lib = c.CDLL(candidate)
                    _configure_libusb(lib)
                    _libusb = lib
                    break
                except (OSError, AttributeError) as error:
                    errors.append(f'{candidate}: {error}')
            if _libusb is None:
                raise RuntimeError('libusb-1.0 was not found. Install it with "brew install libusb",'
                                   ' or set NEOPOD_LIBUSB to the dylib path.')
        if not _libusb_initialized:
            result = _libusb.libusb_init(None)
            if result != 0:
                raise RuntimeError(f'libusb could not start (error {result}).')
            _libusb_initialized = True
            atexit.register(lambda: _libusb.libusb_exit(None))
        return _libusb

    def _libusb_error(lib, code):
        name = lib.libusb_error_name(code)
        return name.decode('ascii', 'replace') if name else str(code)

    def _mac_location(lib, device):
        bus = int(lib.libusb_get_bus_number(device))
        ports = (c.c_uint8 * 8)()
        count = lib.libusb_get_port_numbers(device, ports, len(ports))
        if count < 0:
            count = 0
        return MacRecoveryPath(bus, [ports[index] for index in range(count)])

    def mac_recovery_devices():
        lib = _libusb_handle()
        devices = c.POINTER(c.c_void_p)()
        count = lib.libusb_get_device_list(None, c.byref(devices))
        if count < 0:
            raise RuntimeError(f'libusb could not enumerate USB devices ({_libusb_error(lib, count)}).')
        found = []
        try:
            for index in range(count):
                device = devices[index]
                descriptor = LibusbDeviceDescriptor()
                if lib.libusb_get_device_descriptor(device, c.byref(descriptor)) != 0:
                    continue
                if (descriptor.idVendor, descriptor.idProduct) == (ROCKUSB_VID, ROCKUSB_PID):
                    found.append(_mac_location(lib, device))
        finally:
            lib.libusb_free_device_list(devices, 1)
        return found

    def _select_mac_interface(config):
        preferred, fallback = [], []
        for index in range(config.bNumInterfaces):
            interface = config.interface[index]
            for alt_index in range(interface.num_altsetting):
                alt = interface.altsetting[alt_index]
                in_endpoint = out_endpoint = None
                for endpoint_index in range(alt.bNumEndpoints):
                    address = alt.endpoint[endpoint_index].bEndpointAddress
                    if address & 0x80:
                        in_endpoint = in_endpoint or address
                    else:
                        out_endpoint = out_endpoint or address
                if in_endpoint is None or out_endpoint is None:
                    continue
                entry = (alt.bInterfaceNumber, in_endpoint, out_endpoint)
                identity = (alt.bInterfaceClass, alt.bInterfaceSubClass, alt.bInterfaceProtocol)
                if identity in ((0xff, 6, 5), (8, 6, 0x50)):
                    preferred.append(entry)
                else:
                    fallback.append(entry)
        if preferred:
            return preferred[0]
        if fallback:
            return fallback[0]
        raise RuntimeError('The Rockusb recovery interface has no usable bulk endpoints.')

    def _open_mac_recovery(path=None):
        lib = _libusb_handle()
        devices = c.POINTER(c.c_void_p)()
        count = lib.libusb_get_device_list(None, c.byref(devices))
        if count < 0:
            raise OSError(f'libusb could not enumerate USB devices ({_libusb_error(lib, count)}).')
        handle = c.c_void_p()
        opened = False
        try:
            matches = []
            for index in range(count):
                device = devices[index]
                descriptor = LibusbDeviceDescriptor()
                if lib.libusb_get_device_descriptor(device, c.byref(descriptor)) != 0:
                    continue
                if (descriptor.idVendor, descriptor.idProduct) != (ROCKUSB_VID, ROCKUSB_PID):
                    continue
                location = _mac_location(lib, device)
                if path is None or location == path:
                    matches.append((device, location))
            if len(matches) != 1:
                raise OSError(f'Rockusb recovery interface is not available (found {len(matches)}).'
                              ' Ensure DAP Neo is in recovery mode (Vol+ + Vol- + OK while plugging in USB).')
            device, _ = matches[0]
            result = lib.libusb_open(device, c.byref(handle))
            if result != 0:
                raise OSError(f'Could not open the Rockusb recovery device ({_libusb_error(lib, result)}).'
                              ' Close other flashing tools and retry.')
            opened = True
            config = c.POINTER(LibusbConfigDescriptor)()
            result = lib.libusb_get_active_config_descriptor(device, c.byref(config))
            if result != 0:
                result = lib.libusb_get_config_descriptor(device, 0, c.byref(config))
            if result != 0:
                raise OSError(f'Could not read the Rockusb USB configuration ({_libusb_error(lib, result)}).')
            try:
                interface, in_endpoint, out_endpoint = _select_mac_interface(config.contents)
            finally:
                lib.libusb_free_config_descriptor(config)
            result = lib.libusb_claim_interface(handle, interface)
            if result != 0:
                raise OSError(f'Could not claim the Rockusb recovery interface ({_libusb_error(lib, result)}).'
                              ' Close other flashing tools and retry.')
            return handle, interface, in_endpoint, out_endpoint
        except Exception:
            if opened and handle:
                lib.libusb_close(handle)
            raise
        finally:
            lib.libusb_free_device_list(devices, 1)

    class RockusbMac(RockusbProtocol):
        def __init__(self, path):
            super().__init__()
            self.lib = _libusb_handle()
            self.handle, self.interface, self.in_endpoint, self.out_endpoint = _open_mac_recovery(path)

        def _read(self, length):
            return self._transfer(self.in_endpoint, length, write=False)

        def _write(self, data):
            self._transfer(self.out_endpoint, data, write=True)

        def _transfer(self, endpoint, data_or_length, write=False):
            length = len(data_or_length) if write else data_or_length
            buffer = c.create_string_buffer(bytes(data_or_length) if write else length)
            transferred = c.c_int()
            result = self.lib.libusb_bulk_transfer(
                self.handle, endpoint, buffer, length, c.byref(transferred), 8000)
            if result != 0:
                raise OSError(f'USB transfer failed: {_libusb_error(self.lib, result)}')
            if write and transferred.value != length:
                raise RuntimeError(f'Short write: {transferred.value} / {length}')
            return buffer.raw[:transferred.value]

        def close(self):
            if getattr(self, 'handle', None) is not None:
                if getattr(self, 'interface', None) is not None:
                    self.lib.libusb_release_interface(self.handle, self.interface)
                self.lib.libusb_close(self.handle)
                self.handle = self.interface = None

    def device_path():
        found = mac_recovery_devices()
        if len(found) != 1:
            raise RuntimeError(f'Expected 1 Rockusb recovery interface, found {len(found)}. Ensure DAP Neo is in recovery mode (Vol+ + Vol- + OK while plugging in USB).')
        return found[0]

    def recovery_interface_present(path):
        return path in mac_recovery_devices()


if IS_WINDOWS:
    Rockusb = RockusbWindows
elif IS_MACOS:
    Rockusb = RockusbMac
else:
    class Rockusb:
        def __init__(self, path):
            raise RuntimeError('This flasher supports Windows and macOS only.')

    def device_path():
        raise RuntimeError('This flasher supports Windows and macOS only.')

    def recovery_interface_present(path):
        return False


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


def usb_backend_name():
    """Report the transport used by the frozen app's startup check."""
    if IS_WINDOWS:
        return 'Windows WinUSB'
    if IS_MACOS:
        try:
            return _libusb_handle()._name or 'libusb-1.0'
        except (OSError, RuntimeError):
            return None
    return None


COMPANION_NAMES = ('neo_scan.bin', 'neo_flac.bin', 'neo_mp3.bin',
                   'neo_aac.bin', 'neo_opus.bin', 'boot.rgb565')


def application_root():
    """Directory that holds the helper and its external firmware package."""
    if getattr(sys, 'frozen', False):
        executable = Path(sys.executable).resolve()
        if (IS_MACOS and executable.parent.name == 'MacOS'
                and executable.parent.parent.name == 'Contents'):
            # Firmware belongs beside flash-neo.app, not inside the bundle.
            return executable.parent.parent.parent.parent
        return executable.parent
    return Path(__file__).resolve().parent


def default_image():
    root = application_root()
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


def windows_sd_drives():
    options = {'creationflags': subprocess.CREATE_NO_WINDOW} if IS_WINDOWS else {}
    result = subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command', DRIVE_QUERY],
                            capture_output=True, text=True, timeout=15, **options)
    if result.returncode:
        raise RuntimeError('Windows disk identification failed: ' + result.stderr.strip())
    return json.loads(result.stdout.strip() or '[]')


def mac_system_profiler():
    result = subprocess.run(['/usr/sbin/system_profiler', 'SPUSBDataType', '-json',
                             '-detailLevel', 'full'], capture_output=True, text=True, timeout=30)
    if result.returncode:
        raise RuntimeError('macOS USB identification failed: '
                           + (result.stderr.strip() or result.stdout.strip()))
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError(f'macOS USB identification returned invalid data: {error}') from error


def _usb_children(node):
    for child in node.get('items') or []:
        yield child
        yield from _usb_children(child)


def _usb_number(value):
    if isinstance(value, int):
        return value
    match = re.search(r'0x([0-9a-f]+)', str(value), re.IGNORECASE)
    return int(match[1], 16) if match else None


def _media_list(device):
    return device.get('media') or device.get('Media') or []


def _volume_list(media):
    return media.get('volumes') or media.get('Volumes') or []


def mac_usb_inventory(document=None):
    document = mac_system_profiler() if document is None else document
    devices = []
    for device in _usb_children({'items': document.get('SPUSBDataType') or []}):
        devices.append({
            'vendor': _usb_number(device.get('vendor_id')),
            'product': _usb_number(device.get('product_id')),
            'name': str(device.get('_name') or ''),
            'serial': str(device.get('serial_num') or ''),
            'location': str(device.get('location_id') or ''),
            'media': _media_list(device),
        })
    return devices


def mac_neopod_sd_drives(document=None):
    """Return mounted LUN-1 volumes whose USB parent is the NeoPod."""
    records = []
    for device in mac_usb_inventory(document):
        if (device['vendor'], device['product']) != (NEOPOD_VID, NEOPOD_PID):
            continue
        disks = [media.get('bsd_name') for media in device['media'] if media.get('bsd_name')]
        for media in device['media']:
            if str(media.get('logical_unit', '')).strip() != '1':
                continue
            media_name = str(media.get('_name') or media.get('name') or '')
            if not re.search(r'ECHOPOD.*SD', media_name, re.IGNORECASE):
                continue
            for volume in _volume_list(media):
                root = volume.get('mount_point')
                if not root:
                    continue
                records.append({
                    'root': str(root),
                    'disk': str(media.get('bsd_name') or ''),
                    'id': device['serial'] or device['location'],
                    'usb': device['serial'] or device['location'],
                    'disks': disks,
                    'name': device['name'],
                })
    return records


def mac_eject_player(root):
    root = Path(root).resolve()
    matches = [device for device in mac_neopod_sd_drives()
               if Path(device['root']).resolve() == root]
    if len(matches) != 1:
        raise RuntimeError('Could not uniquely identify this volume as the connected DAP Neo.'
                           ' Eject it with Disk Utility.')
    record = matches[0]
    disks = list(dict.fromkeys(record.get('disks') or [record.get('disk')]))
    if not disks:
        raise RuntimeError('Could not identify the NeoPod storage devices. Eject it with Disk Utility.')
    errors = []
    for disk in reversed(disks):
        result = subprocess.run(['/usr/sbin/diskutil', 'eject', f'/dev/{disk}'],
                                capture_output=True, text=True, timeout=60)
        if result.returncode:
            errors.append((result.stderr or result.stdout).strip())
    still_connected = any((device['vendor'], device['product']) == (NEOPOD_VID, NEOPOD_PID)
                          for device in mac_usb_inventory())
    if still_connected:
        detail = '; '.join(error for error in errors if error) or 'the player is still mounted'
        raise RuntimeError(f'macOS did not eject DAP Neo: {detail}.'
                           ' Close open player files and eject it with Disk Utility.')
    print('[EJECTED] macOS ejected DAP Neo and both storage drives. You can unplug USB.')


def neopod_sd_drives():
    if IS_WINDOWS:
        return windows_sd_drives()
    if IS_MACOS:
        return mac_neopod_sd_drives()
    raise RuntimeError('This flasher supports Windows and macOS only.')


def validate_sd_drive_argument(value):
    candidate = Path(value).expanduser()
    if IS_WINDOWS:
        drive = candidate.resolve()
        if str(drive) != drive.anchor or drive.drive.lower() == os.environ.get('SystemDrive', 'C:').lower():
            raise ValueError('--sd-drive must be a non-system drive root, for example F:\\')
        return drive
    if IS_MACOS:
        if (not candidate.is_absolute() or candidate.parent != Path('/Volumes')
                or candidate.name in ('', '.', '..')):
            raise ValueError('--sd-drive must name a mounted volume root, for example /Volumes/NeoPod')
        return candidate
    raise ValueError('This flasher supports Windows and macOS only.')


def checked_drive_root(value):
    drive = validate_sd_drive_argument(value)
    if IS_MACOS:
        if not drive.is_dir() or not os.path.ismount(drive):
            raise ValueError('--sd-drive must name an existing mounted volume root, for example /Volumes/NeoPod')
        if drive.resolve() == Path('/') or os.stat(drive).st_dev == os.stat('/').st_dev:
            raise ValueError('Refusing to use the macOS system volume as the SD card.')
        return drive.resolve()
    if not drive.is_dir():
        raise ValueError('--sd-drive must name an existing drive root, for example F:\\')
    return drive


def eject_player(root):
    if IS_WINDOWS:
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
    elif IS_MACOS:
        mac_eject_player(root)
    else:
        raise RuntimeError('This flasher supports Windows and macOS only.')


def wait_for_sd(timeout=180.0, sd_drive=None):
    print('Waiting for the NeoPod MicroSD card to mount...')
    print('If reboot stalls, press the pinhole reset once. Keep USB connected and use USB Storage mode.')
    deadline = time.monotonic() + timeout
    last_problem = None
    example = 'F:\\' if IS_WINDOWS else '/Volumes/NeoPod'
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
    raise TimeoutError('The SD card volume did not mount. No companion files were copied.'
                       f' After mounting it, run with --sync-only and the same image,'
                       f' for example: --sd-drive {example}.{detail}')


def prompt_to_eject(root):
    try:
        input('\nPress Enter to safely eject DAP Neo, or close this window to leave it mounted: ')
    except (EOFError, KeyboardInterrupt):
        hint = 'Windows Safely Remove Hardware' if IS_WINDOWS else 'Disk Utility'
        print(f'\nLeft mounted. Use {hint} before unplugging.')
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
        # Validate syntax before flashing; the volume itself may mount only later.
        sd_drive = str(validate_sd_drive_argument(sd_drive))
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
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('image', nargs='?', help='Firmware image to flash')
    parser.add_argument('--reset-only', action='store_true',
                        help='Try restarting a player already in recovery; do not write firmware')
    parser.add_argument('--skip-companions', action='store_true', help='Flash firmware only; do not copy SD companions')
    parser.add_argument('--companions-dir', help='Matching companion source directory (validated against the image manifest)')
    drive_help = ('Explicit MicroSD drive root, e.g. F:\\; otherwise identify the NeoPod SD LUN automatically'
                  if IS_WINDOWS else
                  'Explicit MicroSD volume root, e.g. /Volumes/NeoPod; otherwise identify the NeoPod SD LUN automatically')
    parser.add_argument('--sd-drive', help=drive_help)
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



