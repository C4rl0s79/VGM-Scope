from __future__ import annotations

from PySide6.QtCore import Signal, Qt
from PySide6.QtGui import QPainter, QColor, QPen
from PySide6.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QPushButton, QLineEdit


class ScopeCanvas(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.samples: list[float] = []
        self.setMinimumHeight(90)

    def set_samples(self, samples: list[float]):
        self.samples = samples
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)

        painter.fillRect(self.rect(), QColor(12, 12, 16))
        painter.setPen(QPen(QColor(45, 45, 55), 1))
        painter.drawLine(0, self.height() // 2, self.width(), self.height() // 2)

        if not self.samples:
            return

        painter.setPen(QPen(QColor(0, 230, 180), 1.6))

        count = len(self.samples)
        if count < 2:
            return

        previous_x = 0
        previous_y = self.height() // 2

        for index, sample in enumerate(self.samples):
            x = int(index * (self.width() - 1) / (count - 1))
            y = int((self.height() / 2) - sample * (self.height() / 2 - 6))

            painter.drawLine(previous_x, previous_y, x, y)
            previous_x = x
            previous_y = y


class ScopePanel(QWidget):
    label_edited = Signal(int, str)
    mute_toggled = Signal(int, bool)
    solo_toggled = Signal(int, bool)

    def __init__(self, channel_index: int, label: str, parent=None):
        super().__init__(parent)
        self.channel_index = channel_index
        self.is_muted = False

        self.setStyleSheet(
            "QWidget { background: #101014; border: 1px solid #26262e; border-radius: 6px; }"
        )

        layout = QVBoxLayout(self)
        layout.setContentsMargins(8, 6, 8, 6)
        layout.setSpacing(4)

        top = QHBoxLayout()

        self.label_edit = QLineEdit(label)
        self.label_edit.setStyleSheet(
            "QLineEdit { background: transparent; border: none; color: #d8d8e0; font-weight: 600; }"
        )
        self.label_edit.editingFinished.connect(self._emit_label)

        self.mute_button = QPushButton("M")
        self.mute_button.setCheckable(True)
        self.mute_button.setFixedSize(28, 24)
        self.mute_button.setToolTip("Mute")
        self.mute_button.toggled.connect(self._mute_changed)

        self.solo_button = QPushButton("S")
        self.solo_button.setCheckable(True)
        self.solo_button.setFixedSize(28, 24)
        self.solo_button.setToolTip("Solo — ponowne kliknięcie przywraca wszystkie kanały")
        self.solo_button.toggled.connect(self._solo_changed)

        top.addWidget(self.label_edit)
        top.addStretch()
        top.addWidget(self.mute_button)
        top.addWidget(self.solo_button)

        self.canvas = ScopeCanvas()

        layout.addLayout(top)
        layout.addWidget(self.canvas)

    def _emit_label(self):
        self.label_edited.emit(self.channel_index, self.label_edit.text())

    def _mute_changed(self, checked: bool):
        self.is_muted = checked
        self.mute_toggled.emit(self.channel_index, checked)

    def _solo_changed(self, checked: bool):
        self.solo_toggled.emit(self.channel_index, checked)

    def set_muted(self, muted: bool):
        self.is_muted = muted
        self.mute_button.blockSignals(True)
        self.mute_button.setChecked(muted)
        self.mute_button.blockSignals(False)

    def set_solo(self, solo: bool):
        self.solo_button.blockSignals(True)
        self.solo_button.setChecked(solo)
        self.solo_button.blockSignals(False)

    def set_samples(self, samples: list[float]):
        self.canvas.set_samples(samples)
