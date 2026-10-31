"""GUI integration checks with mocked flash/ejection; never access a device."""
import importlib.util
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

from PySide6.QtWidgets import QApplication, QMessageBox

spec = importlib.util.spec_from_file_location('gui', Path(__file__).parents[1] / 'flash_neo_gui.py')
gui = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gui)
app = QApplication.instance() or QApplication([sys.argv[0]])


class GuiTests(unittest.TestCase):
    def test_worker_passes_update_options_and_returns_mount(self):
        options = dict(image_path='test.IMG', sync_only=True, sd_drive='F:\\')
        job = gui.Job('update', options)
        results = []
        job.result.connect(lambda *args: results.append(args))
        with patch.object(gui.backend, 'flash', return_value=Path('F:/')) as flash:
            job.run()
        flash.assert_called_once_with(**options)
        self.assertEqual([(Path('F:/'), '')], results)

    def test_backend_failure_is_reported_without_success(self):
        job = gui.Job('update', {})
        results = []
        job.result.connect(lambda *args: results.append(args))
        with patch.object(gui.backend, 'flash', side_effect=ValueError('Manifest mismatch')):
            job.run()
        self.assertEqual([(None, 'Manifest mismatch')], results)

    def test_eject_uses_verified_mount(self):
        job = gui.Job('eject', {'root': Path('F:/')})
        with patch.object(gui.backend, 'eject_player') as eject:
            job.run()
        eject.assert_called_once_with(Path('F:/'))

    def test_ready_gate_and_busy_lock(self):
        with patch.object(gui.backend, 'default_image', return_value=Path('test.IMG')):
            window = gui.Window()
        window.detector.stop()
        window.show_page(1)
        self.assertFalse(window.next.isEnabled())
        window.recovery_ready = True
        window.refresh()
        self.assertTrue(window.next.isEnabled())
        window.job = gui.Job('update', {})
        window.refresh()
        self.assertFalse(window.next.isEnabled())
        self.assertFalse(window.image.isEnabled())
        self.assertFalse(window.eject.isEnabled())
        window.job = None
        window.close()

    def test_passive_connection_indicator_and_update_gate_track_disconnect(self):
        window = gui.Window()
        window.detector.stop()
        window.show_page(1)
        with patch.object(gui.backend, 'live_recovery_path', return_value='live'), patch.object(gui.backend, 'flash') as flash:
            window.check_connection()
            self.assertTrue(window.next.isEnabled())
            self.assertEqual('Connected in recovery mode', window.connection_title.text())
            flash.assert_not_called()
        with patch.object(gui.backend, 'live_recovery_path', return_value=None), patch.object(gui.backend, 'flash') as flash:
            window.check_connection()
            self.assertFalse(window.next.isEnabled())
            self.assertEqual('Not connected in recovery mode', window.connection_title.text())
            flash.assert_not_called()
        window.close()

    def test_live_log_visible_at_minimum_window_size(self):
        window = gui.Window()
        window.detector.stop()
        window.show_page(2)
        window.resize(940, 620)
        window.show()
        app.processEvents()
        bottom = window.log.mapTo(window, window.log.rect().bottomRight())
        self.assertTrue(window.log.isVisible())
        self.assertLess(bottom.y(), window.height())
        self.assertGreater(window.log.height(), 100)
        window.close()

    def test_log_stream_assembles_partial_prints(self):
        lines = []
        stream = gui.LogStream(lines.append)
        stream.write('Writing ')
        stream.write('Slot 1\n50%\n')
        stream.write('Done')
        stream.flush()
        self.assertEqual(['Writing Slot 1', '50%', 'Done'], lines)


if __name__ == '__main__':
    unittest.main()
