from __future__ import annotations

import sys
from pathlib import Path

from PySide6.QtCore import Qt, QTimer
from PySide6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QPushButton,
    QFileDialog, QLabel, QSlider, QGridLayout, QSplitter, QListWidget, QMessageBox
)

from app.libvgm_engine import LibVgmEngine
from app.playlist import load_playlist
from app.scope_panel import ScopePanel
from app.labels import LabelStore


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("VGM Scope")
        self.resize(1280, 760)

        self.engine = LibVgmEngine()
        self.labels = LabelStore(Path("labels.json"))
        self.playlist: list[Path] = []
        self.current_index = -1

        self.scope_widgets: list[ScopePanel] = []
        self.timer = QTimer(self)
        self.timer.setInterval(33)
        self.timer.timeout.connect(self.update_scopes)
        self.timer.start()

        self._build_ui()

    def _build_ui(self):
        central = QWidget()
        layout = QVBoxLayout(central)

        controls = QHBoxLayout()
        self.open_button = QPushButton("Otwórz plik / M3U")
        self.open_button.clicked.connect(self.open_file)

        self.play_button = QPushButton("▶ Play")
        self.play_button.clicked.connect(self.play)

        self.pause_button = QPushButton("⏸ Pauza")
        self.pause_button.clicked.connect(self.engine.pause)

        self.stop_button = QPushButton("⏹ Stop")
        self.stop_button.clicked.connect(self.engine.stop)

        self.prev_button = QPushButton("⏮")
        self.prev_button.clicked.connect(self.previous)

        self.next_button = QPushButton("⏭")
        self.next_button.clicked.connect(self.next)

        self.volume = QSlider(Qt.Horizontal)
        self.volume.setRange(0, 100)
        self.volume.setValue(80)
        self.volume.valueChanged.connect(self.engine.set_volume)

        for widget in (
            self.open_button, self.play_button, self.pause_button,
            self.stop_button, self.prev_button, self.next_button
        ):
            controls.addWidget(widget)

        controls.addWidget(QLabel("Głośność"))
        controls.addWidget(self.volume)
        layout.addLayout(controls)

        self.status = QLabel("Nie wybrano pliku")
        layout.addWidget(self.status)

        splitter = QSplitter(Qt.Horizontal)

        self.playlist_widget = QListWidget()
        self.playlist_widget.itemDoubleClicked.connect(self.playlist_double_clicked)
        splitter.addWidget(self.playlist_widget)

        self.scope_area = QWidget()
        self.scope_layout = QGridLayout(self.scope_area)
        self.scope_layout.setSpacing(8)
        splitter.addWidget(self.scope_area)
        splitter.setSizes([280, 1000])

        layout.addWidget(splitter)
        self.setCentralWidget(central)
        self.setAcceptDrops(True)

    def dragEnterEvent(self, event):
        if event.mimeData().hasUrls():
            event.acceptProposedAction()

    def dropEvent(self, event):
        paths = [Path(url.toLocalFile()) for url in event.mimeData().urls()]
        if paths:
            self.load_path(paths[0])

    def open_file(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Wybierz VGM, VGZ lub M3U", "",
            "VGM / VGZ / M3U (*.vgm *.vgz *.m3u *.m3u8);;Wszystkie pliki (*)"
        )
        if path:
            self.load_path(Path(path))

    def load_path(self, path: Path):
        path = path.resolve()
        if path.suffix.lower() in (".m3u", ".m3u8"):
            self.playlist = load_playlist(path)
        else:
            self.playlist = [path]

        if not self.playlist:
            QMessageBox.warning(self, "VGM Scope", "Playlista jest pusta.")
            return

        self.playlist_widget.clear()
        for item in self.playlist:
            self.playlist_widget.addItem(item.name)

        self.current_index = 0
        self.playlist_widget.setCurrentRow(0)
        self.load_current()

    def playlist_double_clicked(self, item):
        self.current_index = self.playlist_widget.row(item)
        self.load_current()

    def load_current(self):
        if not self.playlist or not (0 <= self.current_index < len(self.playlist)):
            return

        path = self.playlist[self.current_index]
        try:
            self.engine.load(str(path))
        except Exception as exc:
            QMessageBox.critical(self, "Błąd", f"Nie można otworzyć pliku:\n{exc}")
            return

        self.status.setText(f"{path.name} — {self.engine.title}")
        self.rebuild_scopes()
        self.engine.play()

    def rebuild_scopes(self):
        while self.scope_layout.count():
            item = self.scope_layout.takeAt(0)
            if item.widget():
                item.widget().deleteLater()

        self.scope_widgets.clear()

        names = self.engine.channel_names()
        saved = self.labels.get_labels(str(self.engine.current_path))

        columns = 3
        for index, auto_name in enumerate(names):
            label = saved.get(str(index), auto_name)
            panel = ScopePanel(index, label)
            panel.label_edited.connect(self.save_label)
            panel.mute_toggled.connect(self.engine.set_channel_mute)
            panel.solo_toggled.connect(self.solo_channel)

            self.scope_layout.addWidget(panel, index // columns, index % columns)
            self.scope_widgets.append(panel)

    def save_label(self, channel_index: int, text: str):
        self.labels.set_label(str(self.engine.current_path), channel_index, text)

    def solo_channel(self, channel_index: int, enabled: bool):
        if enabled:
            for panel in self.scope_widgets:
                muted = panel.channel_index != channel_index
                panel.set_muted(muted)
                self.engine.set_channel_mute(panel.channel_index, muted)
        else:
            for panel in self.scope_widgets:
                panel.set_muted(False)
                self.engine.set_channel_mute(panel.channel_index, False)

    def play(self):
        if not self.engine.current_path:
            self.open_file()
            return
        self.engine.play()

    def previous(self):
        if self.playlist:
            self.current_index = max(0, self.current_index - 1)
            self.playlist_widget.setCurrentRow(self.current_index)
            self.load_current()

    def next(self):
        if self.playlist:
            self.current_index = min(len(self.playlist) - 1, self.current_index + 1)
            self.playlist_widget.setCurrentRow(self.current_index)
            self.load_current()

    def update_scopes(self):
        if not self.engine.current_path:
            return

        buffers = self.engine.get_channel_buffers()
        for panel in self.scope_widgets:
            if panel.channel_index < len(buffers):
                panel.set_samples(buffers[panel.channel_index])

    def keyPressEvent(self, event):
        if event.key() == Qt.Key_Space:
            self.engine.toggle()
        elif event.key() == Qt.Key_Right and event.modifiers() & Qt.ControlModifier:
            self.next()
        elif event.key() == Qt.Key_Left and event.modifiers() & Qt.ControlModifier:
            self.previous()
        else:
            super().keyPressEvent(event)


def main():
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
