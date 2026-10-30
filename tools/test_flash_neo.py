"""Mock-only reset tests. No device is opened and no firmware is written."""
import contextlib
import importlib.util
import io
import os
from pathlib import Path
import struct
import unittest
from unittest.mock import Mock, patch

if os.name == "nt":
    spec = importlib.util.spec_from_file_location("flash_neo", Path(__file__).parents[1] / "flash-neo.py")
    flash = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(flash)


@unittest.skipUnless(os.name == "nt", "Windows Rockusb script")
class ResetTests(unittest.TestCase):
    def usb(self):
        usb = flash.Rockusb.__new__(flash.Rockusb)
        usb.tag = 100
        usb.h = 123
        usb.write_h = 456
        return usb

    def test_reset_packet_and_status_for_both_normal_boot_modes(self):
        for subcode in (0, 1):
            usb = self.usb()
            status = struct.pack('<IIIB', 0x53425355, 101, 0, 0)
            with patch.object(flash, 'transfer', side_effect=[b'', status]) as transfer:
                usb.reset(subcode)
            packet = transfer.call_args_list[0].args[1]
            self.assertEqual(31, len(packet))
            self.assertEqual((0x43425355, 101, 0, 0, 0, 6), struct.unpack('<IIIBBB', packet[:15]))
            self.assertEqual(bytes([0xFF, subcode]) + bytes(14), packet[15:])

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

    def test_disconnect_before_status_is_confirmed_by_live_probe(self):
        usb = Mock()
        usb.reset.side_effect = OSError('device disconnected before status')
        with patch.object(flash, 'recovery_interface_present', return_value=False), \
             contextlib.redirect_stdout(io.StringIO()):
            self.assertTrue(flash.restart_player(usb, 'mock'))
        usb.reset.assert_called_once_with(0)
        usb.close.assert_called_once()

    def test_still_connected_normal_reset_tries_msc_reset(self):
        first, second = Mock(), Mock()
        with patch.object(flash, 'Rockusb', return_value=second), \
             patch.object(flash, 'recovery_interface_present', side_effect=[True, False]), \
             patch.object(flash.time, 'monotonic', side_effect=[0, 0, 4, 4, 4]), \
             patch.object(flash.time, 'sleep'), contextlib.redirect_stdout(io.StringIO()):
            self.assertTrue(flash.restart_player(first, 'mock'))
        first.reset.assert_called_once_with(0)
        second.reset.assert_called_once_with(1)
        first.close.assert_called_once()
        second.close.assert_called_once()

    def test_staying_in_recovery_is_reported_as_unconfirmed(self):
        first, second = Mock(), Mock()
        with patch.object(flash, 'Rockusb', return_value=second), \
             patch.object(flash, 'recovery_interface_present', return_value=True), \
             patch.object(flash.time, 'monotonic', side_effect=[0, 0, 4, 4, 4, 8]), \
             patch.object(flash.time, 'sleep'), contextlib.redirect_stdout(io.StringIO()):
            self.assertFalse(flash.restart_player(first, 'mock'))

    def test_close_is_idempotent(self):
        usb = self.usb()
        with patch.object(flash.k, 'CloseHandle') as close:
            usb.close()
            usb.close()
            self.assertEqual(2, close.call_count)
        self.assertIsNone(usb.h)
        self.assertIsNone(usb.write_h)


if __name__ == '__main__':
    unittest.main()
