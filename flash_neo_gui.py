"""NeoPod graphical updater for Windows and macOS. The CLI remains in flash-neo.py."""
import contextlib
import importlib.util
import json
from pathlib import Path
import re
import sys

from PySide6.QtCore import Qt, QThread, Signal, QTimer
from PySide6.QtGui import QColor, QFont, QIcon, QPainter, QPixmap
from PySide6.QtWidgets import (QApplication, QCheckBox, QComboBox, QFileDialog,
    QFrame, QHBoxLayout, QLabel, QLineEdit, QMainWindow, QMessageBox,
    QPlainTextEdit, QProgressBar, QPushButton, QStackedWidget, QSizePolicy, QVBoxLayout, QWidget)

if getattr(sys, 'frozen', False):
    import flash_neo_backend as backend
else:
    spec = importlib.util.spec_from_file_location('flash_neo', Path(__file__).with_name('flash-neo.py'))
    backend = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(backend)


class LogStream:
    def __init__(self, emit):
        self.emit, self.buffer = emit, ''

    def write(self, text):
        self.buffer += text
        while '\n' in self.buffer:
            line, self.buffer = self.buffer.split('\n', 1)
            if line.strip():
                self.emit(line.strip())
        return len(text)

    def flush(self):
        if self.buffer:
            self.emit(self.buffer)
            self.buffer = ''


class Job(QThread):
    log = Signal(str)
    result = Signal(object, str)

    def __init__(self, action, options):
        super().__init__()
        self.action, self.options = action, options

    def run(self):
        stream = LogStream(self.log.emit)
        try:
            with contextlib.redirect_stdout(stream), contextlib.redirect_stderr(stream):
                if self.action == 'validate':
                    image = Path(self.options['image_path'])
                    raw = backend.checked_firmware_bytes(image)
                    if not self.options['skip_companions']:
                        backend.companion_payload(image)
                    print(f'Package verified: {image.name} · {len(raw):,} firmware bytes')
                    mounted = {'image': str(image)}
                elif self.action == 'eject':
                    backend.eject_player(self.options['root'])
                    mounted = None
                else:
                    mounted = backend.flash(**self.options)
            stream.flush()
            self.result.emit(mounted, '')
        except Exception as error:
            stream.flush()
            self.result.emit(None, str(error))


STYLE = """
QWidget { background: #10151d; color: #edf2f7; font-family: '__FONT__'; font-size: 13px; }
QLabel { background: transparent; }
QFrame#sidebar { background: #151e29; border-right: 1px solid #293340; }
QFrame#card { background: #19222e; border: 1px solid #2a3645; border-radius: 14px; }
QFrame#card QLabel, QFrame#card QCheckBox { background: transparent; }
QLabel#muted { color: #9caec2; }
QLabel#eyebrow { color: #66e4c0; font-weight: 700; font-size: 11px; }
QLabel#title { font-size: 28px; font-weight: 650; }
QLabel#heading { font-size: 17px; font-weight: 600; }
QPushButton { background: #253244; border: 1px solid #37485d; border-radius: 8px; padding: 10px 18px; font-weight: 600; }
QPushButton:hover { background: #32445a; }
QPushButton:disabled { background: #1c2632; color: #637185; border-color: #2a3440; }
QPushButton#primary { background: #66e4c0; color: #10251f; border: none; padding: 14px 24px; font-size: 15px; }
QPushButton#primary:hover { background: #8cf1d3; }
QPushButton#primary:disabled { background: #304a45; color: #839e96; }
QLineEdit, QComboBox { background: #101821; border: 1px solid #38495d; border-radius: 7px; padding: 6px 10px; min-height: 22px; }
QLineEdit:focus, QComboBox:focus { border-color: #66e4c0; }
QComboBox QAbstractItemView { background: #19222e; selection-background-color: #304a45; }
QCheckBox { spacing: 9px; }
QCheckBox::indicator { width: 17px; height: 17px; }
QCheckBox::indicator:unchecked { background: #101821; border: 1px solid #61758b; border-radius: 4px; }
QCheckBox::indicator:checked { background: #66e4c0; border: 1px solid #66e4c0; border-radius: 4px; image: none; }
QProgressBar { background: #0f1720; border: none; border-radius: 4px; height: 8px; color: transparent; }
QProgressBar::chunk { background: #66e4c0; border-radius: 4px; }
QPlainTextEdit { background: #0d131b; border: 1px solid #293645; border-radius: 8px; color: #aebfd0; font-family: '__MONO__'; font-size: 11px; padding: 8px; }
"""

UI_FONT = 'Helvetica Neue' if sys.platform == 'darwin' else 'Segoe UI'
MONO_FONT = 'Menlo' if sys.platform == 'darwin' else 'Consolas'
STYLE = STYLE.replace('__FONT__', UI_FONT).replace('__MONO__', MONO_FONT)


def label(text, name=None, wrap=False):
    item = QLabel(text)
    if name:
        item.setObjectName(name)
    item.setWordWrap(wrap)
    return item


def card():
    frame = QFrame()
    frame.setObjectName('card')
    layout = QVBoxLayout(frame)
    layout.setContentsMargins(22, 18, 22, 18)
    layout.setSpacing(12)
    return frame, layout


class Window(QMainWindow):
    def __init__(self):
        super().__init__()
        self.job, self.mounted = None, None
        self.recovery_ready = False
        self.completed_update = False
        self.setWindowTitle('NeoPod · Firmware Studio')
        self.resize(1040, 740)
        self.setMinimumSize(940, 620)
        icon = QPixmap(64, 64)
        icon.fill(QColor('#66e4c0'))
        painter = QPainter(icon)
        painter.setPen(QColor('#10251f'))
        painter.setFont(QFont(UI_FONT, 36, QFont.Weight.Bold))
        painter.drawText(icon.rect(), Qt.AlignmentFlag.AlignCenter, 'N')
        painter.end()
        self.setWindowIcon(QIcon(icon))
        central = QWidget()
        self.setCentralWidget(central)
        shell = QHBoxLayout(central)
        shell.setContentsMargins(0, 0, 0, 0)
        shell.setSpacing(0)
        sidebar = QFrame()
        sidebar.setObjectName('sidebar')
        sidebar.setFixedWidth(215)
        side = QVBoxLayout(sidebar)
        side.setContentsMargins(24, 30, 24, 26)
        side.setSpacing(12)
        side.addWidget(label('N E O P O D', 'heading'))
        side.addWidget(label('FIRMWARE STUDIO', 'eyebrow'))
        side.addSpacing(38)
        self.steps = []
        for text in ('01   Firmware package', '02   Connect your player', '03   Update & finish'):
            item = label(text)
            item.setContentsMargins(8, 13, 4, 13)
            side.addWidget(item)
            self.steps.append(item)
        side.addStretch()
        side.addWidget(label('FOSI AUDIO DAP NEO', 'eyebrow'))
        side.addWidget(label('A guided update.\nYour music, carried forward.', 'muted', True))
        shell.addWidget(sidebar)
        main = QVBoxLayout()
        main.setContentsMargins(30, 28, 30, 24)
        main.setSpacing(12)
        shell.addLayout(main, 1)
        self.eyebrow = label('', 'eyebrow')
        self.title = label('', 'title')
        self.subtitle = label('', 'muted', True)
        main.addWidget(self.eyebrow)
        main.addWidget(self.title)
        main.addWidget(self.subtitle)
        self.pages = QStackedWidget()
        main.addWidget(self.pages, 1)

        page, body = self.page()
        frame, layout = card()
        layout.addWidget(label('Choose a firmware image', 'heading'))
        row = QHBoxLayout()
        self.image = QLineEdit()
        self.image.setPlaceholderText('Select a DP3 firmware image (.IMG)')
        self.browse = QPushButton('Browse…')
        self.browse.clicked.connect(self.pick_image)
        row.addWidget(self.image, 1)
        row.addWidget(self.browse)
        layout.addLayout(row)
        layout.addWidget(label('Keep the image, SHA256SUMS.txt and sd-card in the same package folder.', 'muted', True))
        layout.addSpacing(8)
        layout.addWidget(label('Update mode', 'muted'))
        self.mode = QComboBox()
        self.mode.addItems(['Full update · firmware + companions', 'Finish SD companions only', 'Firmware only · skip companions'])
        self.mode.setSizePolicy(QSizePolicy.Policy.Ignored, QSizePolicy.Policy.Fixed)
        layout.addWidget(self.mode)
        layout.addWidget(label('SD card location (optional)', 'muted'))
        self.drive = QLineEdit()
        self.drive.setPlaceholderText('Automatic SD detection')
        drive_hint = 'F:\\' if sys.platform == 'win32' else '/Volumes/NeoPod'
        self.drive.setToolTip('Leave blank for automatic detection. Override only with the actual MicroSD volume root'
                              f' assigned by this computer, for example {drive_hint}.')
        layout.addWidget(self.drive)
        body.addWidget(frame)
        self.package_status = label('The package is checked before connecting. No firmware is written until you select Start update on step 2.', 'muted', True)
        body.addWidget(self.package_status)
        body.addStretch()
        self.pages.addWidget(page)

        page, body = self.page()
        frame, layout = card()
        connection_row = QHBoxLayout()
        self.connection_icon = QLabel()
        self.connection_icon.setFixedSize(14, 14)
        self.connection_title = label('Not connected in recovery mode', 'heading', True)
        connection_row.addWidget(self.connection_icon)
        connection_row.addSpacing(6)
        connection_row.addWidget(self.connection_title, 1)
        layout.addLayout(connection_row)
        self.connection_status = label('Enter recovery using the buttons below. This indicator updates automatically.', 'muted', True)
        layout.addWidget(self.connection_status)
        self.ready = QCheckBox('My player’s SD card is mounted and ready for the companion copy.')
        self.ready.toggled.connect(self.refresh)
        layout.addWidget(self.ready)
        body.addWidget(frame)
        self.manual, layout = card()
        layout.addWidget(label('Enter recovery mode', 'heading'))
        layout.addWidget(label('1. Unplug USB and turn the player off.\n2. Hold Vol+ + Vol− + OK while reconnecting USB.\n3. Release after 3–5 seconds. The screen stays black.', 'muted', True))
        layout.addWidget(label('Recovery is detected automatically. Before entering recovery, select USB Storage mode in the player’s settings.', 'muted', True))
        body.addWidget(self.manual)
        body.addStretch(1)
        self.pages.addWidget(page)

        page, body = self.page()
        self.status = label('Ready to update', 'heading', True)
        body.addWidget(self.status)
        self.progress = QProgressBar()
        self.progress.setRange(0, 100)
        self.progress.setValue(0)
        self.progress.setTextVisible(False)
        body.addWidget(self.progress)
        self.activity = label('Keep USB connected until the operation finishes.', 'muted', True)
        body.addWidget(self.activity)
        self.log = QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.setMinimumHeight(120)
        self.log.setMaximumBlockCount(4000)
        self.log.setPlaceholderText('Update activity will appear here.')
        body.addWidget(self.log, 1)
        row = QHBoxLayout()
        self.save = QPushButton('Save log')
        self.save.clicked.connect(self.save_log)
        self.eject = QPushButton('Safely eject')
        self.eject.clicked.connect(self.eject_player)
        row.addWidget(self.save)
        row.addStretch()
        row.addWidget(self.eject)
        body.addLayout(row)
        self.pages.addWidget(page)
        footer = QHBoxLayout()
        self.back = QPushButton('←  Back')
        self.back.clicked.connect(self.go_back)
        self.next = QPushButton('Continue  →')
        self.next.setObjectName('primary')
        self.next.clicked.connect(self.advance)
        footer.addWidget(self.back)
        footer.addStretch()
        footer.addWidget(self.next)
        main.addLayout(footer)
        for control in self.findChildren(QWidget):
            if isinstance(control, (QLineEdit, QComboBox, QPushButton)):
                control.ensurePolished()
                control.setMinimumHeight(max(control.sizeHint().height(), control.fontMetrics().height() + 20))
        self.image.textChanged.connect(self.refresh)
        self.mode.currentIndexChanged.connect(self.mode_changed)
        try:
            self.image.setText(str(backend.default_image()))
        except FileNotFoundError:
            pass
        self.detector = QTimer(self)
        self.detector.setInterval(1000)
        self.detector.timeout.connect(self.check_connection)
        self.detector.start()
        self.update_connection_indicator()
        self.show_page(0)

    @staticmethod
    def page():
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(0, 6, 0, 6)
        layout.setSpacing(12)
        return page, layout

    def mode_changed(self):
        self.ready.setChecked(False)
        self.recovery_ready = False
        self.refresh()

    def show_page(self, index):
        self.pages.setCurrentIndex(index)
        titles = ('Choose your firmware.', 'Get your player ready.', 'Update your Neo.')
        subtitles = ('Select a complete package for your DAP Neo.',
                     'Enter recovery mode, then wait for the green connection indicator.',
                     'Live progress and the complete activity log, together.')
        self.eyebrow.setText(f'STEP {index + 1} OF 3')
        self.title.setText(titles[index])
        self.subtitle.setText(subtitles[index])
        for number, step in enumerate(self.steps):
            step.setStyleSheet('color: #66e4c0; background: #223830; border-radius: 8px; font-weight: 600;' if number == index else 'color: #9caec2; background: transparent;')
        self.refresh()

    def refresh(self):
        if not hasattr(self, 'next'):
            return
        busy = self.job is not None
        index = self.pages.currentIndex()
        sync = self.mode.currentIndex() == 1
        self.ready.setVisible(sync)
        self.manual.setVisible(not sync)
        self.connection_icon.setVisible(not sync)
        self.back.setVisible(index > 0)
        self.back.setEnabled(not busy)
        self.next.setText(('Check package  →', 'Start companion copy  →' if sync else 'Start update  →', 'New update')[index])
        can_start = bool(self.image.text().strip()) if index == 0 else (self.ready.isChecked() if sync else self.recovery_ready) if index == 1 else True
        self.next.setEnabled(not busy and can_start)
        for widget in (self.image, self.browse, self.mode, self.drive, self.ready):
            widget.setEnabled(not busy)
        self.eject.setEnabled(not busy and self.mounted is not None)

    def pick_image(self):
        path, _ = QFileDialog.getOpenFileName(self, 'Choose firmware', self.image.text(), 'Firmware (*.IMG *.img)')
        if path:
            self.image.setText(path)
            self.image.setToolTip(path)

    def advance(self):
        index = self.pages.currentIndex()
        if index == 0:
            image = Path(self.image.text().strip()).resolve()
            if not image.is_file():
                QMessageBox.warning(self, 'Choose a firmware image', 'Select an existing .IMG file first.')
                return
            self.image.setText(str(image))
            self.log.clear()
            self.package_status.setText('Checking firmware and matching companion files…')
            self.launch('validate', {'image_path': str(image), 'skip_companions': self.mode.currentIndex() == 2})
        elif index == 1:
            self.begin()
        else:
            self.completed_update = False
            self.show_page(0)

    def go_back(self):
        if self.job is None:
            self.show_page(max(0, self.pages.currentIndex() - 1))

    def check_connection(self):
        if self.pages.currentIndex() != 1 or self.job is not None or self.mode.currentIndex() == 1:
            return
        connected = backend.live_recovery_path() is not None
        if connected != self.recovery_ready:
            self.log.appendPlainText('Recovery connected.' if connected else 'Recovery disconnected.')
        self.recovery_ready = connected
        self.update_connection_indicator()
        self.refresh()

    def update_connection_indicator(self):
        color = '#66e4a0' if self.recovery_ready else '#ff7585'
        self.connection_icon.setStyleSheet(f'background: {color}; border-radius: 7px;')
        self.connection_title.setStyleSheet(f'color: {color}; background: transparent; font-weight: 600; font-size: 17px;')
        self.connection_title.setText('Connected in recovery mode' if self.recovery_ready else 'Not connected in recovery mode')
        self.connection_status.setText('Your DAP Neo is ready. Select Start update to continue.' if self.recovery_ready else
            'Enter recovery using the buttons below. A normal USB storage connection does not count as recovery mode.')

    def begin(self):
        image = Path(self.image.text().strip())
        if self.mode.currentIndex() != 1:
            self.check_connection()
            if not self.recovery_ready:
                return
            answer = QMessageBox.question(self, 'Update your DAP Neo?',
                f'Write {image.name} to both firmware slots?\n\nKeep USB connected until the update finishes.',
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.Cancel,
                QMessageBox.StandardButton.Cancel)
            if answer != QMessageBox.StandardButton.Yes:
                return
        self.mounted = None
        self.completed_update = False
        self.show_page(2)
        self.launch('update', dict(image_path=str(image), skip_companions=self.mode.currentIndex() == 2,
                    sync_only=self.mode.currentIndex() == 1, sd_drive=self.drive.text().strip() or None))

    def launch(self, action, options):
        if self.job is not None:
            return
        if action in ('update', 'eject'):
            self.progress.setRange(0, 0)
            self.status.setText('Safely ejecting…' if action == 'eject' else 'Checking your package…')
        self.job = Job(action, options)
        self.job.log.connect(self.on_log)
        self.job.result.connect(self.completed)
        self.job.finished.connect(self.thread_finished)
        self.refresh()
        self.job.start()

    def on_log(self, text):
        self.log.appendPlainText(text)
        match = re.search(r'(\d+)%', text)
        if match:
            self.progress.setRange(0, 100)
            self.progress.setValue(int(match[1]))
        elif text.startswith(('Writing ', 'Verifying ', 'Resetting ', 'Waiting ', 'Updated and verified:')):
            self.status.setText(text)
            self.progress.setRange(0, 0)

    def completed(self, result, error):
        action = self.job.action
        if action == 'validate':
            if error:
                self.package_status.setText('Package needs attention: ' + error)
            else:
                self.package_status.setText('Package verified.')
                self.recovery_ready = False
                self.show_page(1)
                if self.mode.currentIndex() == 1:
                    self.connection_title.setStyleSheet('color: #edf2f7; background: transparent; font-size: 17px; font-weight: 600;')
                    self.connection_title.setText('Mount your MicroSD card')
                    self.connection_status.setText('The player must be running normally in USB Storage mode. Recovery is not needed for companion-only updates.')
                else:
                    self.update_connection_indicator()
            return
        self.progress.setRange(0, 100)
        if error:
            self.progress.setValue(0)
            self.status.setText('Update needs attention' if action == 'update' else 'Player remains mounted')
            self.activity.setText(error)
            self.log.appendPlainText('[ERROR] ' + error)
        else:
            self.progress.setValue(100)
            self.mounted = result
            self.completed_update = True
            if action == 'eject':
                self.status.setText('Safely ejected.')
                self.activity.setText('Your player is ready to unplug. Enjoy your next listen.')
            elif result:
                self.status.setText('Update complete.')
                self.activity.setText('Firmware companions are verified. Select Safely eject before unplugging.')
            else:
                self.status.setText('Firmware written.')
                self.activity.setText('See the activity log for reboot status. Companion files were not updated.')

    def thread_finished(self):
        action = self.job.action
        self.job.deleteLater()
        self.job = None
        self.refresh()
        if action == 'validate' and self.pages.currentIndex() == 1:
            QTimer.singleShot(0, self.check_connection)

    def eject_player(self):
        self.launch('eject', {'root': self.mounted})

    def save_log(self):
        path, _ = QFileDialog.getSaveFileName(self, 'Save update log', 'neopod-update.txt', 'Text (*.txt)')
        if path:
            try:
                Path(path).write_text(self.log.toPlainText(), encoding='utf-8')
            except OSError as error:
                QMessageBox.warning(self, 'Could not save log', str(error))

    def closeEvent(self, event):
        if self.job is not None:
            QMessageBox.information(self, 'Operation in progress', 'Keep this window open and USB connected until the current operation finishes.')
            event.ignore()
        else:
            event.accept()


def main():
    app = QApplication([sys.argv[0]])
    app.setApplicationName('NeoPod Firmware Studio')
    app.setApplicationDisplayName('NeoPod Firmware Studio')
    app.setStyle('Fusion')
    app.setStyleSheet(STYLE)
    window = Window()
    window.show()
    # Build verification: render the real UI and report discovery without USB.
    if '--smoke-test' in sys.argv:
        window.detector.stop()
        target = Path(sys.argv[sys.argv.index('--smoke-test') + 1])
        def capture():
            window.grab().save(str(target.with_suffix('.png')))
            window.show_page(1)
            window.grab().save(str(target.with_name(target.stem + '-step2.png')))
            window.show_page(2)
            window.grab().save(str(target.with_name(target.stem + '-step3.png')))
            target.write_text(json.dumps({'image': window.image.text(), 'page_count': window.pages.count(), 'eject_enabled': window.eject.isEnabled(), 'console_visible': window.log.isVisible(), 'usb_backend': backend.usb_backend_name()}), encoding='utf-8')
            app.quit()
        QTimer.singleShot(700, capture)
    sys.exit(app.exec())


if __name__ == '__main__':
    main()
