"""macOS USB inventory, SD detection, and ejection tests; no device is touched.

The parsers run on every platform so Windows CI still checks the macOS logic.
Only tests that need real POSIX mount points or the libusb ctypes structs are
skipped outside macOS.
"""
import contextlib
import importlib.util
import io
import json
import os
import stat
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

SUPPORTED = os.name == "nt" or sys.platform == "darwin"
IS_MACOS = sys.platform == "darwin"
if SUPPORTED:
    spec = importlib.util.spec_from_file_location("flash_neo", Path(__file__).parents[1] / "flash-neo.py")
    flash = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(flash)


def profiler_document(media=None, vendor="0x1209  (4617)", product="0x0001  (1)"):
    """Minimal system_profiler JSON with a bus wrapping the NeoPod."""
    if media is None:
        media = [
            {'_name': 'DAP Neo', 'logical_unit': '0', 'bsd_name': 'disk4', 'volumes': []},
            {'_name': 'ECHOPOD SD card', 'logical_unit': '1', 'bsd_name': 'disk5',
             'volumes': [{'_name': 'NeoPod', 'mount_point': '/Volumes/NeoPod'}]},
        ]
    return {'SPUSBDataType': [{
        '_name': 'USB31Bus',
        'items': [{
            '_name': 'DAP Neo',
            'vendor_id': vendor,
            'product_id': product,
            'serial_num': 'NEOSERIAL',
            'location_id': '0x01100000',
            'media': media,
        }],
    }]}


@unittest.skipUnless(SUPPORTED, "Windows/macOS Rockusb script")
class MacInventoryTests(unittest.TestCase):
    def test_nested_usb_tree_is_flattened(self):
        devices = flash.mac_usb_inventory(profiler_document())
        self.assertEqual(['USB31Bus', 'DAP Neo'], [device['name'] for device in devices])
        self.assertEqual((flash.NEOPOD_VID, flash.NEOPOD_PID),
                         (devices[1]['vendor'], devices[1]['product']))
        self.assertEqual('NEOSERIAL', devices[1]['serial'])

    def test_hex_ids_accept_plain_ints_and_dump_suffixes(self):
        self.assertEqual(0x1209, flash._usb_number('0x1209  (4617)'))
        self.assertEqual(0x0001, flash._usb_number(1))
        self.assertIsNone(flash._usb_number('n/a'))

    def test_capitalized_media_and_volumes_keys_are_accepted(self):
        device = {'_name': 'Neo', 'vendor_id': '0x1209', 'product_id': '0x0001',
                  'Media': [{'logical_unit': 1, '_name': 'ECHOPOD SD',
                             'Volumes': [{'mount_point': '/Volumes/NeoPod'}]}]}
        drives = flash.mac_neopod_sd_drives({'SPUSBDataType': [device]})
        self.assertEqual('/Volumes/NeoPod', drives[0]['root'])

    def test_system_profiler_errors_are_reported(self):
        failure = Mock(returncode=1, stdout='', stderr='USB service unavailable')
        with patch.object(flash.subprocess, 'run', return_value=failure), \
                self.assertRaisesRegex(RuntimeError, 'USB service unavailable'):
            flash.mac_system_profiler()
        invalid = Mock(returncode=0, stdout='not json', stderr='')
        with patch.object(flash.subprocess, 'run', return_value=invalid), \
                self.assertRaisesRegex(RuntimeError, 'invalid data'):
            flash.mac_system_profiler()


@unittest.skipUnless(SUPPORTED, "Windows/macOS Rockusb script")
class MacSdDriveTests(unittest.TestCase):
    def test_lun_one_echopod_media_is_the_only_match(self):
        drives = flash.mac_neopod_sd_drives(profiler_document())
        self.assertEqual(1, len(drives))
        record = drives[0]
        self.assertEqual('/Volumes/NeoPod', record['root'])
        self.assertEqual('disk5', record['disk'])
        self.assertEqual(['disk4', 'disk5'], record['disks'])
        self.assertEqual('NEOSERIAL', record['usb'])

    def test_wrong_vendor_is_ignored(self):
        self.assertEqual([], flash.mac_neopod_sd_drives(
            profiler_document(vendor='0x1234  (4660)')))

    def test_lun_zero_and_non_sd_media_are_ignored(self):
        media = [{'logical_unit': '0', '_name': 'ECHOPOD SD', 'bsd_name': 'disk4',
                  'volumes': [{'mount_point': '/Volumes/NeoPod'}]}]
        self.assertEqual([], flash.mac_neopod_sd_drives(profiler_document(media=media)))
        media = [{'logical_unit': '1', '_name': 'DAP Neo', 'bsd_name': 'disk4',
                  'volumes': [{'mount_point': '/Volumes/NeoPod'}]}]
        self.assertEqual([], flash.mac_neopod_sd_drives(profiler_document(media=media)))

    def test_unmounted_technically_matching_media_is_ignored(self):
        media = [{'logical_unit': '1', '_name': 'ECHOPOD SD card', 'bsd_name': 'disk5', 'volumes': []}]
        self.assertEqual([], flash.mac_neopod_sd_drives(profiler_document(media=media)))


@unittest.skipUnless(SUPPORTED, "Windows/macOS Rockusb script")
class MacEjectTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.card = Path(temp.name) / 'NeoPod'
        self.card.mkdir()
        self.record = {'root': str(self.card), 'disk': 'disk5',
                       'disks': ['disk4', 'disk5'], 'usb': 'NEOSERIAL'}

    def eject(self, disks=None, inventory=(), results=((0, ''), (0, '')), **record):
        matches = [] if disks == [] else [{**self.record, 'disks': disks or self.record['disks'], **record}]
        calls = [Mock(returncode=code, stdout='', stderr=error) for code, error in results]
        with patch.object(flash, 'mac_neopod_sd_drives', return_value=matches), \
                patch.object(flash, 'mac_usb_inventory', return_value=list(inventory)), \
                patch.object(flash.subprocess, 'run', side_effect=calls) as run, \
                contextlib.redirect_stdout(io.StringIO()) as output:
            flash.mac_eject_player(self.card)
        return run, output.getvalue()

    def test_all_media_eject_inner_first(self):
        run, output = self.eject()
        self.assertEqual([['/usr/sbin/diskutil', 'eject', '/dev/disk5'],
                          ['/usr/sbin/diskutil', 'eject', '/dev/disk4']],
                         [call.args[0] for call in run.call_args_list])
        self.assertIn('[EJECTED]', output)

    def test_unknown_volume_is_never_ejected(self):
        run = Mock()
        with patch.object(flash, 'mac_neopod_sd_drives', return_value=[]), \
                patch.object(flash.subprocess, 'run', run):
            with self.assertRaisesRegex(RuntimeError, 'Could not uniquely identify'):
                flash.mac_eject_player(self.card)
        run.assert_not_called()

    def test_failure_while_still_connected_is_not_reported_as_ejected(self):
        with self.assertRaisesRegex(RuntimeError, 'still mounted|did not eject'):
            self.eject(inventory=[{'vendor': flash.NEOPOD_VID, 'product': flash.NEOPOD_PID}])

    def test_diskutil_error_detail_is_preserved(self):
        with self.assertRaisesRegex(RuntimeError, 'Resource busy'):
            self.eject(results=((1, 'Resource busy'), (1, 'Resource busy')),
                       inventory=[{'vendor': flash.NEOPOD_VID, 'product': flash.NEOPOD_PID}])

    def test_disappearing_device_confirms_ejection(self):
        _, output = self.eject(inventory=[])
        self.assertIn('[EJECTED]', output)


@unittest.skipUnless(SUPPORTED, "Windows/macOS Rockusb script")
class MacDriveArgumentTests(unittest.TestCase):
    def mac(self):
        return patch.multiple(flash, IS_WINDOWS=False, IS_MACOS=True)

    def test_relative_and_non_volumes_paths_are_rejected(self):
        with self.mac():
            for value in ('NeoPod', 'relative/path', 'C:\\NeoPod'):
                with self.assertRaises(ValueError, msg=value):
                    flash.validate_sd_drive_argument(value)

    def test_other_platforms_reject_every_drive(self):
        with patch.multiple(flash, IS_WINDOWS=False, IS_MACOS=False):
            with self.assertRaises(ValueError):
                flash.validate_sd_drive_argument('/Volumes/NeoPod')

    @unittest.skipUnless(IS_MACOS, "needs real POSIX mount points")
    def test_mounted_volume_is_accepted_and_system_volume_rejected(self):
        with self.mac():
            self.assertEqual(Path('/Volumes/NeoPod'),
                             flash.validate_sd_drive_argument('/Volumes/NeoPod'))
            for value in ('/', '/Volumes', '/Volumes/..'):
                with self.assertRaises(ValueError):
                    flash.checked_drive_root(value)

    @unittest.skipUnless(IS_MACOS, "needs real POSIX mount points")
    def test_unmounted_volume_is_rejected(self):
        with self.mac(), self.assertRaises(ValueError):
            flash.checked_drive_root('/Volumes/DefinitelyNotMounted')

    @unittest.skipUnless(IS_MACOS, "needs macOS st_dev comparison")
    def test_same_device_as_root_is_rejected(self):
        directory = Mock(st_dev=7, st_mode=stat.S_IFDIR)
        with self.mac(), patch.object(flash.os.path, 'ismount', return_value=True), \
                patch.object(flash.os, 'stat', return_value=directory):
            with self.assertRaisesRegex(ValueError, 'system volume'):
                flash.checked_drive_root('/Volumes/NeoPod')


@unittest.skipUnless(IS_MACOS, "libusb structs are macOS-only")
class MacLibusbLayoutTests(unittest.TestCase):
    def test_descriptor_layouts_match_libusb(self):
        import ctypes
        self.assertEqual(18, ctypes.sizeof(flash.LibusbDeviceDescriptor))
        self.assertEqual(8, flash.LibusbDeviceDescriptor.idVendor.offset)
        self.assertEqual(2, flash.LibusbEndpointDescriptor.bEndpointAddress.offset)
        self.assertEqual(4, flash.LibusbEndpointDescriptor.wMaxPacketSize.offset)
        self.assertEqual(2, flash.LibusbConfigDescriptor.wTotalLength.offset)
        self.assertEqual(7, flash.LibusbConfigDescriptor.bmAttributes.offset)
        self.assertEqual(8, flash.LibusbConfigDescriptor.MaxPower.offset)

    def test_mac_recovery_path_is_hashable_and_descriptive(self):
        first = flash.MacRecoveryPath(3, [1, 2])
        self.assertEqual(first, flash.MacRecoveryPath(3, (1, 2)))
        self.assertNotEqual(first, flash.MacRecoveryPath(3, (1, 3)))
        self.assertEqual('USB bus 3, port 1.2', str(first))
        self.assertEqual(hash(first), hash(flash.MacRecoveryPath(3, [1, 2])))


if __name__ == '__main__':
    unittest.main()
