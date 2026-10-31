"""Mock USB and temporary-directory tests: never flash a physical device."""
import contextlib
import hashlib
import importlib.util
import io
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import Mock, patch

if os.name == "nt":
    spec = importlib.util.spec_from_file_location("flash_neo", Path(__file__).parents[1] / "flash-neo.py")
    flash = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(flash)


@unittest.skipUnless(os.name == "nt", "Windows Rockusb script")
class ImageDiscoveryTests(unittest.TestCase):
    def test_frozen_image_is_beside_exe_not_extracted_script_or_cwd(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package, extracted = root / 'package', root / 'extracted'
            package.mkdir()
            extracted.mkdir()
            expected = package / 'DP3_V174.IMG'
            expected.touch()
            (extracted / 'DP3_V999.IMG').touch()
            with patch.object(flash.sys, 'frozen', True, create=True), \
                 patch.object(flash.sys, 'executable', str(package / 'flash-neo.exe')), \
                 patch.object(flash, '__file__', str(extracted / 'flash-neo.py')):
                self.assertEqual(expected.resolve(), flash.default_image())

    def test_python_still_discovers_highest_release_image(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'release').mkdir()
            (root / 'DP3_V173.IMG').touch()
            expected = root / 'release' / 'DP3_V174.IMG'
            expected.touch()
            with patch.object(flash.sys, 'frozen', False, create=True), \
                 patch.object(flash, '__file__', str(root / 'flash-neo.py')):
                self.assertEqual(expected.resolve(), flash.default_image())


@unittest.skipUnless(os.name == "nt", "Windows recovery connection")
class RecoveryConnectionTests(unittest.TestCase):
    def test_live_interface_is_required(self):
        with patch.object(flash, 'device_path', return_value='mock'), patch.object(flash, 'recovery_interface_present', return_value=True):
            self.assertEqual('mock', flash.live_recovery_path())

    def test_stale_registry_entry_is_not_connected(self):
        with patch.object(flash, 'device_path', return_value='stale'), patch.object(flash, 'recovery_interface_present', return_value=False):
            self.assertIsNone(flash.live_recovery_path())

    def test_missing_ambiguous_or_inaccessible_device_is_not_connected(self):
        for error in (RuntimeError('Expected 1 interface'), PermissionError('Access denied')):
            with patch.object(flash, 'device_path', side_effect=error):
                self.assertIsNone(flash.live_recovery_path())


@unittest.skipUnless(os.name == "nt", "Windows Rockusb script")
class ResetTests(unittest.TestCase):
    def usb(self):
        usb = flash.Rockusb.__new__(flash.Rockusb)
        usb.tag, usb.h, usb.write_h = 100, 123, 456
        return usb

    def test_reset_packet_and_status_for_documented_boot_modes(self):
        for subcode in (0, 1, 4):
            usb = self.usb()
            status = struct.pack('<IIIB', 0x53425355, 101, 0, 0)
            with patch.object(flash, 'transfer', side_effect=[b'', status]) as transfer:
                usb.reset(subcode)
            packet = transfer.call_args_list[0].args[1]
            self.assertEqual(31, len(packet))
            self.assertEqual((0x43425355, 101, 0, 0, 0, 6), struct.unpack('<IIIBBB', packet[:15]))
            self.assertEqual(bytes([0xFF, subcode]) + bytes(14), packet[15:])

    def test_poweroff_and_maskrom_are_rejected(self):
        for subcode in (2, 3, 255):
            with self.assertRaises(ValueError):
                self.usb().reset(subcode)

    def test_bad_status_is_not_silently_accepted(self):
        for status in (b'', struct.pack('<IIIB', 0x53425355, 101, 0, 1),
                       struct.pack('<IIIB', 0x53425355, 999, 0, 0),
                       struct.pack('<IIIB', 0x53425355, 101, 1, 0)):
            with patch.object(flash, 'transfer', side_effect=[b'', status]):
                with self.assertRaises(RuntimeError):
                    self.usb().reset()

    def test_failed_reset_write_is_not_hidden(self):
        with patch.object(flash, 'transfer', side_effect=OSError('write failed')):
            with self.assertRaises(OSError):
                self.usb().reset()

    def test_disconnect_before_status_still_waits_for_live_confirmation(self):
        usb = Mock()
        usb.reset.side_effect = OSError('detached before status')
        with patch.object(flash, 'wait_recovery_disconnect', return_value=True), contextlib.redirect_stdout(io.StringIO()):
            self.assertTrue(flash.restart_player(usb, 'mock'))
        usb.reset.assert_called_once_with(0)
        usb.close.assert_called_once()

    def test_reset_fallback_order_and_handles_closed(self):
        first, second, third = Mock(), Mock(), Mock()
        with patch.object(flash, 'Rockusb', side_effect=[second, third]), patch.object(flash, 'wait_recovery_disconnect', side_effect=[False, False, True]), contextlib.redirect_stdout(io.StringIO()):
            self.assertTrue(flash.restart_player(first, 'mock'))
        for usb, mode in ((first, 0), (second, 4), (third, 1)):
            usb.reset.assert_called_once_with(mode)
            usb.close.assert_called_once()

    def test_staying_in_recovery_is_unconfirmed(self):
        with patch.object(flash, 'Rockusb', return_value=Mock()), patch.object(flash, 'wait_recovery_disconnect', return_value=False), contextlib.redirect_stdout(io.StringIO()):
            self.assertFalse(flash.restart_player(Mock(), 'mock'))

    def test_transient_missing_pipe_is_not_accepted(self):
        with patch.object(flash, 'recovery_interface_present', side_effect=[False, True, False, False, False]) as probe, patch.object(flash.time, 'sleep'):
            self.assertTrue(flash.wait_recovery_disconnect('mock'))
        self.assertEqual(5, probe.call_count)

    def test_close_is_idempotent(self):
        usb = self.usb()
        with patch.object(flash.k, 'CloseHandle') as close:
            usb.close()
            usb.close()
            self.assertEqual(2, close.call_count)


@unittest.skipUnless(os.name == "nt", "Windows flash helper")
class CompanionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.image = self.base / 'DP3_V173.IMG'
        self.image.write_bytes(b'test firmware')
        self.source = self.base / 'sd-card' / '.echo'
        self.source.mkdir(parents=True)
        self.payload = {name: ('new ' + name).encode() for name in flash.COMPANION_NAMES}
        for name, data in self.payload.items():
            (self.source / name).write_bytes(data)
        self.manifest = self.base / 'SHA256SUMS.txt'
        self.manifest.write_text(''.join(f'{hashlib.sha256(data).hexdigest()}  {name}\n' for name, data in [(self.image.name, self.image.read_bytes()), *self.payload.items()]))
        self.card = self.base / 'mounted-card'
        self.card.mkdir()

    def test_package_hashes_are_checked(self):
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(self.payload, flash.companion_payload(self.image))
        (self.source / 'neo_scan.bin').write_bytes(b'wrong build')
        with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
            flash.companion_payload(self.image)

    def test_wrong_firmware_and_missing_manifest_rejected_before_usb(self):
        self.image.write_bytes(b'wrong firmware')
        with patch.object(flash, 'Rockusb') as usb, self.assertRaises(ValueError):
            flash.flash(self.image)
        usb.assert_not_called()
        self.manifest.unlink()
        with self.assertRaises(FileNotFoundError):
            flash.companion_payload(self.image)

    def test_relative_package_manifest_and_recovery_alias(self):
        self.manifest.write_text(self.manifest.read_text().replace('  neo_', '  sd-card/.echo/neo_').replace('  boot.', '  sd-card/.echo/boot.'))
        alias = self.base / 'RKNANOFW.IMG'
        alias.write_bytes(self.image.read_bytes())
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(self.payload, flash.companion_payload(alias))

    def test_only_allowlisted_files_change_and_old_files_are_backed_up(self):
        echo = self.card / '.echo'
        echo.mkdir()
        private = {'settings.bin': b'settings', 'bluetooth.bin': b'paired', 'library.db': b'library'}
        for name, data in private.items():
            (echo / name).write_bytes(data)
        (echo / 'neo_scan.bin').write_bytes(b'old scanner')
        with contextlib.redirect_stdout(io.StringIO()):
            flash.sync_companions(self.card, self.payload)
        for name, data in {**self.payload, **private}.items():
            self.assertEqual(data, (echo / name).read_bytes())
        self.assertEqual(b'old scanner', next(echo.glob('.neopod-update-*/backup/neo_scan.bin')).read_bytes())

    def test_failed_replacement_rolls_back_already_replaced_files(self):
        echo = self.card / '.echo'
        echo.mkdir()
        old = {name: ('old ' + name).encode() for name in flash.COMPANION_NAMES}
        for name, data in old.items():
            (echo / name).write_bytes(data)
        replace = os.replace
        def fail_second(source, target):
            if Path(target).name == 'neo_flac.bin':
                raise OSError('simulated card failure')
            return replace(source, target)
        with patch.object(flash.os, 'replace', side_effect=fail_second), contextlib.redirect_stdout(io.StringIO()), self.assertRaises(OSError):
            flash.sync_companions(self.card, self.payload)
        for name, data in old.items():
            self.assertEqual(data, (echo / name).read_bytes())

    def test_non_allowlisted_payload_is_rejected(self):
        with self.assertRaises(ValueError):
            flash.sync_companions(self.card, {**self.payload, 'settings.bin': b'bad'})
        self.assertFalse((self.card / '.echo').exists())

    def test_wait_for_mount_and_refuse_ambiguous_devices(self):
        with patch.object(flash, 'neopod_sd_drives', side_effect=[[], [{'root': 'F:', 'id': 'neo'}]]), patch.object(flash, 'checked_drive_root', return_value=self.card), patch.object(flash.time, 'sleep'), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(self.card, flash.wait_for_sd())
        with patch.object(flash, 'neopod_sd_drives', return_value=[{}, {}]), contextlib.redirect_stdout(io.StringIO()), self.assertRaisesRegex(ValueError, 'Multiple'):
            flash.wait_for_sd()

    def test_mount_timeout_copies_nothing(self):
        with patch.object(flash.time, 'monotonic', side_effect=[0, 2]), contextlib.redirect_stdout(io.StringIO()), self.assertRaises(TimeoutError):
            flash.wait_for_sd(timeout=1)
        self.assertFalse((self.card / '.echo').exists())

    def valid_image(self):
        old_digest = hashlib.sha256(self.image.read_bytes()).hexdigest()
        data = bytearray(13631488)
        data[0x1f8:0x200] = b'RKnanoFW'
        self.image.write_bytes(data)
        digest = hashlib.sha256(data).hexdigest()
        self.manifest.write_text(self.manifest.read_text().replace(old_digest, digest))
        return bytes(data)

    def test_sync_only_does_not_open_recovery_or_write_firmware(self):
        self.valid_image()
        with patch.object(flash, 'Rockusb') as usb, patch.object(flash, 'wait_for_sd', return_value=self.card), contextlib.redirect_stdout(io.StringIO()):
            flash.flash(self.image, sync_only=True)
        usb.assert_not_called()
        self.assertEqual(self.payload['neo_scan.bin'], (self.card / '.echo/neo_scan.bin').read_bytes())

    def test_flash_finishes_both_slots_before_reset_and_sync(self):
        raw = self.valid_image()
        usb = Mock()
        usb.command.side_effect = [b'chip', b'flash', raw[:512]]
        events = []
        def reset(*args):
            self.assertEqual(3328, usb.write_lba.call_count)
            events.append('reset')
            return True
        def mounted(*args):
            usb.close.assert_called()
            events.append('mounted')
            return self.card
        with patch.object(flash, 'device_path', return_value='mock'), patch.object(flash, 'Rockusb', return_value=usb), patch.object(flash, 'restart_player', side_effect=reset), patch.object(flash, 'wait_for_sd', side_effect=mounted), contextlib.redirect_stdout(io.StringIO()):
            flash.flash(self.image)
        self.assertEqual(['reset', 'mounted'], events)
        self.assertEqual(self.payload['neo_scan.bin'], (self.card / '.echo/neo_scan.bin').read_bytes())

    def test_failed_firmware_write_never_resets_or_copies(self):
        self.valid_image()
        usb = Mock()
        usb.command.side_effect = [b'chip', b'flash']
        usb.write_lba.side_effect = OSError('write failure')
        with patch.object(flash, 'device_path', return_value='mock'), patch.object(flash, 'Rockusb', return_value=usb), patch.object(flash, 'restart_player') as reset, patch.object(flash, 'wait_for_sd') as mounted, contextlib.redirect_stdout(io.StringIO()), self.assertRaises(OSError):
            flash.flash(self.image)
        reset.assert_not_called()
        mounted.assert_not_called()
        usb.close.assert_called_once()

    def test_system_drive_or_subdirectory_is_rejected(self):
        with self.assertRaises(ValueError):
            flash.checked_drive_root(Path(os.environ.get('SystemDrive', 'C:') + '/'))
        with self.assertRaises(ValueError):
            flash.checked_drive_root(self.card)

    def test_enter_ejects_and_closing_input_leaves_mounted(self):
        with patch('builtins.input', return_value=''), patch.object(flash, 'eject_player') as eject:
            flash.prompt_to_eject(self.card)
        eject.assert_called_once_with(self.card)
        for problem in (EOFError, KeyboardInterrupt):
            with patch('builtins.input', side_effect=problem), patch.object(flash, 'eject_player') as eject, contextlib.redirect_stdout(io.StringIO()):
                flash.prompt_to_eject(self.card)
            eject.assert_not_called()

    def test_eject_targets_only_the_matched_neopod_usb_parent(self):
        instance = r'USB\VID_1209&PID_0001\TESTSERIAL'
        disks = [{'root': str(self.card), 'usb': instance}]
        with patch.object(flash, 'neopod_sd_drives', return_value=disks), patch.object(flash, 'request_device_eject') as eject, contextlib.redirect_stdout(io.StringIO()):
            flash.eject_player(self.card)
        eject.assert_called_once_with(instance)
        disks[0]['usb'] = r'USB\VID_1234&PID_0000\OTHER'
        with patch.object(flash, 'neopod_sd_drives', return_value=disks), patch.object(flash, 'request_device_eject') as eject, self.assertRaises(RuntimeError):
            flash.eject_player(self.card)
        eject.assert_not_called()

    def test_windows_veto_is_not_reported_as_safe_to_unplug(self):
        with patch('builtins.input', return_value=''), patch.object(flash, 'eject_player', side_effect=RuntimeError('Windows refused safe removal')), contextlib.redirect_stdout(io.StringIO()) as output:
            flash.prompt_to_eject(self.card)
        self.assertIn('[NOT EJECTED]', output.getvalue())
        self.assertNotIn('[EJECTED]', output.getvalue())

    def test_eject_api_rejection_propagates(self):
        cfg = Mock()
        cfg.CM_Locate_DevNodeW.return_value = 0
        cfg.CM_Request_Device_EjectW.return_value = 23
        with patch.object(flash.c, 'WinDLL', return_value=cfg), self.assertRaisesRegex(RuntimeError, 'refused safe removal'):
            flash.request_device_eject('mock-device')

    def test_windows_inventory_response_and_failure(self):
        result = Mock(returncode=0, stdout='[{"root":"F:/","disk":2,"id":"verified"}]')
        with patch.object(flash.subprocess, 'run', return_value=result):
            self.assertEqual('F:/', flash.neopod_sd_drives()[0]['root'])
        result.returncode = 1
        result.stderr = 'CIM unavailable'
        with patch.object(flash.subprocess, 'run', return_value=result), self.assertRaises(RuntimeError):
            flash.neopod_sd_drives()


if __name__ == '__main__':
    unittest.main()
