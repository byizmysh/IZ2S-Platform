#!/usr/bin/python3
"""Dolphin service-menu chooser; the window/tray owns and cleans up its exporter."""
import argparse
import json
import os
import sys
from pathlib import Path
from PyQt6.QtCore import QLockFile, QProcess, QTimer, Qt
from PyQt6.QtGui import QAction, QIcon
from PyQt6.QtNetwork import QLocalServer, QLocalSocket
from PyQt6.QtWidgets import (QApplication, QDialog, QHBoxLayout, QLabel, QLineEdit,
    QListWidget, QListWidgetItem, QMenu, QMessageBox, QPlainTextEdit, QProgressBar,
    QPushButton, QStyle, QSystemTrayIcon, QVBoxLayout)

ROOT = Path(__file__).resolve().parents[2]
STORAGE = {2: 'Game card', 4: 'Internal storage', 5: 'SD'}


class Chooser(QDialog):
    def __init__(self, app):
        super().__init__()
        self.app = app
        self.process = None
        self.command = None
        self.buffer = b''
        self.games = []
        self.url = None
        self.current_game = None
        self.pending_title = None
        self.requested_title = None
        self.quitting = False
        self.failed = False
        self.stopping = False
        self.setWindowTitle('Switch 2 - Games in Dolphin')
        icon = QIcon.fromTheme('folder-remote', app.style().standardIcon(QStyle.StandardPixmap.SP_DirIcon))
        self.setWindowIcon(icon)
        self.resize(720, 590)
        layout = QVBoxLayout(self)
        heading = QLabel('Choose a game on Switch 2')
        heading.setStyleSheet('font-size: 22px; font-weight: 600; margin: 8px 0')
        layout.addWidget(heading)
        self.status = QLabel('Checking FS RPC…')
        self.status.setWordWrap(True)
        layout.addWidget(self.status)
        self.search = QLineEdit()
        self.search.setPlaceholderText('Search by name or Title ID')
        self.search.textChanged.connect(self.filter_games)
        layout.addWidget(self.search)
        self.list = QListWidget()
        self.list.itemDoubleClicked.connect(lambda _: self.connect_game())
        self.list.currentItemChanged.connect(self.update_buttons)
        layout.addWidget(self.list, 1)
        row = QHBoxLayout()
        self.refresh = QPushButton('Refresh games')
        self.refresh.clicked.connect(lambda: self.start('list', '--refresh'))
        row.addWidget(self.refresh)
        self.disconnect = QPushButton('Disconnect')
        self.disconnect.clicked.connect(self.stop)
        row.addWidget(self.disconnect)
        row.addStretch()
        self.open = QPushButton('Open in Dolphin')
        self.open.setDefault(True)
        self.open.clicked.connect(self.connect_game)
        row.addWidget(self.open)
        layout.addLayout(row)
        self.progress = QProgressBar()
        self.progress.setRange(0, 0)
        self.progress.setFixedHeight(5)
        self.progress.setTextVisible(False)
        layout.addWidget(self.progress)
        hint = QLabel('Game files are read-only. Closing this window keeps the connection\n'
                      'in the system tray. Disconnect closes the export and keeps FS RPC active.')
        hint.setWordWrap(True)
        hint.setStyleSheet('color: palette(mid); margin-top: 6px')
        layout.addWidget(hint)
        details = QPushButton('Details')
        details.setCheckable(True)
        self.details = QPlainTextEdit()
        self.details.setReadOnly(True)
        self.details.setMaximumBlockCount(500)
        self.details.setMaximumHeight(125)
        self.details.hide()
        details.toggled.connect(self.details.setVisible)
        layout.addWidget(details)
        layout.addWidget(self.details)
        self.tray = QSystemTrayIcon(icon, self)
        self.tray.setToolTip('Switch 2 - Games in Dolphin')
        menu = QMenu()
        for label, callback in [('Choose a game…', self.reveal), ('Open folder', self.open_folder),
                                ('Disconnect', self.stop), ('Quit', self.quit)]:
            action = QAction(label, self)
            action.triggered.connect(callback)
            menu.addAction(action)
        self.tray.setContextMenu(menu)
        self.tray.activated.connect(lambda reason: self.reveal() if reason == QSystemTrayIcon.ActivationReason.Trigger else None)
        self.tray.show()
        self.start('list')

    def reveal(self):
        self.showNormal()
        self.raise_()
        self.activateWindow()

    def update_buttons(self, *_):
        busy = self.stopping or (self.process is not None and not self.url)
        self.refresh.setEnabled(self.process is None)
        self.open.setEnabled(self.list.currentItem() is not None and not busy)
        self.disconnect.setEnabled(self.process is not None and self.command == 'connect' and not self.stopping)

    def filter_games(self, *_):
        term = self.search.text().casefold().strip()
        selected = self.list.currentItem().data(Qt.ItemDataRole.UserRole) if self.list.currentItem() else None
        self.list.clear()
        for game in self.games:
            if term not in (game['name'] + ' ' + game['title']).casefold():
                continue
            storage = ', '.join(STORAGE.get(n, str(n)) for n in game['storages'])
            item = QListWidgetItem(f"{game['name']}\n{storage} · {game['title'].upper()}")
            item.setData(Qt.ItemDataRole.UserRole, game['title'])
            item.setToolTip(game['name'])
            self.list.addItem(item)
            if game['title'] == selected:
                self.list.setCurrentItem(item)
        if self.list.count() and not self.list.currentItem():
            self.list.setCurrentRow(0)
        self.update_buttons()

    def start(self, command, value=None):
        if self.process is not None:
            return
        self.command, self.failed, self.stopping = command, False, False
        self.buffer = b''
        self.url = None
        self.progress.show()
        self.status.setText('Reading games from the console…' if command == 'list' else 'Connecting the selected game…')
        self.process = QProcess(self)
        self.process.setWorkingDirectory(str(ROOT))
        self.process.readyReadStandardOutput.connect(self.receive)
        self.process.readyReadStandardError.connect(self.stderr)
        self.process.errorOccurred.connect(self.process_error)
        self.process.finished.connect(self.finished)
        args = [str(ROOT / 'tools/dolphin-fs-rpc/bridge.cjs'), command]
        if value:
            args.append(value)
        self.process.start(str(ROOT / 'tools/node'), args)
        self.update_buttons()

    def receive(self):
        if not self.process:
            return
        self.buffer += bytes(self.process.readAllStandardOutput())
        while b'\n' in self.buffer:
            line, self.buffer = self.buffer.split(b'\n', 1)
            try:
                self.handle_event(json.loads(line))
            except (ValueError, KeyError) as error:
                self.details.appendPlainText(line.decode(errors='replace') + '\n' + str(error))

    def stderr(self):
        if self.process:
            self.details.appendPlainText(bytes(self.process.readAllStandardError()).decode(errors='replace'))

    def handle_event(self, event):
        kind = event['type']
        if kind in ('progress', 'detail'):
            self.details.appendPlainText(event['message'])
            if kind == 'progress' and not self.stopping:
                self.status.setText(event['message'])
        elif kind == 'games':
            self.games = event['games']
            self.filter_games()
            count = sum(game['title'] != 'storage' for game in self.games)
            self.status.setText(f"FS RPC connected · {count} games + console storage · {event['session']['host']}")
        elif kind == 'ready':
            if self.stopping:
                return
            self.url = event['url']
            self.current_game = event['game']['title']
            self.status.setText(f"Connected: {event['game']['name']}\n{self.url}")
            self.tray.setToolTip('Switch 2 · ' + event['game']['name'])
            self.progress.hide()
            self.open_folder()
            self.update_buttons()
        elif kind == 'error':
            self.failed = True
            self.status.setText(event['message'])
            self.details.appendPlainText(event['message'])
            self.reveal()

    def process_error(self, error):
        if error == QProcess.ProcessError.FailedToStart:
            self.failed = True
            self.status.setText('Could not start the helper. Check your Node.js installation.')
            self.finished(-1)

    def finished(self, code, *_):
        if self.process is None:
            return
        self.receive()
        self.stderr()
        was_connect = self.command == 'connect'
        self.process.deleteLater()
        self.process = None
        self.url = None
        self.current_game = None
        self.progress.hide()
        if was_connect and not self.failed:
            self.status.setText('Export disconnected. Choose another game to connect.')
        elif code != 0 and not self.failed:
            self.status.setText('The helper stopped with an error. Open Details.')
        self.update_buttons()
        if self.quitting:
            self.app.quit()
        elif self.requested_title:
            title, self.requested_title = self.requested_title, None
            QTimer.singleShot(0, lambda: self.select_game(title))
        elif self.pending_title:
            title, self.pending_title = self.pending_title, None
            QTimer.singleShot(0, lambda: self.start('connect', title))

    def select_game(self, title):
        self.reveal()
        if self.command == 'list' and self.process is not None:
            self.requested_title = title
            return
        self.search.clear()
        for index in range(self.list.count()):
            if self.list.item(index).data(Qt.ItemDataRole.UserRole) == title:
                self.list.setCurrentRow(index)
                if self.stopping:
                    self.pending_title = title
                else:
                    self.connect_game()
                return
        self.status.setText('Game not found. Refresh the game list.')

    def connect_game(self):
        if self.stopping:
            return
        item = self.list.currentItem()
        if item is None:
            return
        title = item.data(Qt.ItemDataRole.UserRole)
        if self.url and title == self.current_game:
            self.open_folder()
        elif self.process is not None:
            if self.command == 'connect':
                self.pending_title = title
                self.stop(clear_pending=False)
        else:
            self.start('connect', title)

    def open_folder(self):
        if self.url:
            if not QProcess.startDetached('/usr/bin/dolphin', [self.url])[0]:
                self.status.setText('The export is ready, but Dolphin could not start: ' + self.url)

    def stop(self, *_args, clear_pending=True):
        if clear_pending:
            self.pending_title = None
        if self.process is not None:
            self.stopping = True
            self.status.setText('Disconnecting and releasing game files…')
            self.progress.show()
            self.process.terminate()
            self.update_buttons()

    def quit(self):
        self.quitting = True
        if self.process is not None:
            self.stop()
        else:
            self.app.quit()

    def closeEvent(self, event):
        if self.process is not None and self.command == 'connect':
            if QSystemTrayIcon.isSystemTrayAvailable():
                self.hide()
                event.ignore()
            else:
                self.quit()
                event.ignore()
        else:
            self.quit()
            event.accept()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--smoke-test', action='store_true')
    parser.add_argument('--game', help='Open this installed Title ID through the chooser')
    options = parser.parse_args()
    if options.game:
        import re
        if not re.fullmatch(r'[0-9a-fA-F]{16}', options.game):
            parser.error('--game must be a 16-digit hexadecimal Title ID')
        options.game = options.game.lower()
    app = QApplication(sys.argv[:1])
    app.setApplicationName('run21-switch2-dolphin')
    app.setDesktopFileName('run21-switch2-dolphin')
    app.setQuitOnLastWindowClosed(False)
    runtime = Path(os.environ.get('XDG_RUNTIME_DIR', '/tmp')) / f'run21-dolphin-{os.getuid()}'
    lock = QLockFile(str(runtime) + '.lock')
    socket_name = str(runtime) + '.sock'
    if not lock.tryLock(100):
        socket = QLocalSocket()
        socket.connectToServer(socket_name)
        if socket.waitForConnected(2000):
            socket.write(json.dumps({'game': options.game}).encode())
            socket.waitForBytesWritten(1000)
        return 0
    QLocalServer.removeServer(socket_name)
    server = QLocalServer()
    server.setSocketOptions(QLocalServer.SocketOption.UserAccessOption)
    if not server.listen(socket_name):
        QMessageBox.critical(None, 'Switch 2', server.errorString())
        return 1
    window = Chooser(app)
    def reveal():
        socket = server.nextPendingConnection()
        socket.disconnected.connect(socket.deleteLater)
        def receive():
            try:
                request = json.loads(bytes(socket.readAll()).decode())
                if request.get('game'):
                    window.select_game(request['game'])
                else:
                    window.reveal()
            except (ValueError, AttributeError):
                window.reveal()
            socket.disconnectFromServer()
        socket.readyRead.connect(receive)
        if socket.bytesAvailable():
            receive()
    server.newConnection.connect(reveal)
    window.show()
    if options.game:
        window.select_game(options.game)
    if options.smoke_test:
        QTimer.singleShot(2500, window.quit)
    result = app.exec()
    server.close()
    return result


if __name__ == '__main__':
    sys.exit(main())
