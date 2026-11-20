from __future__ import annotations

import math
import random
import time
from abc import ABC, abstractmethod
from pathlib import Path
from typing import List


class VgmEngine(ABC):
    current_path: Path | None = None
    title: str = ""

    @abstractmethod
    def load(self, path: str) -> None: ...
    @abstractmethod
    def play(self) -> None: ...
    @abstractmethod
    def pause(self) -> None: ...
    @abstractmethod
    def stop(self) -> None: ...
    @abstractmethod
    def toggle(self) -> None: ...
    @abstractmethod
    def set_volume(self, value: int) -> None: ...
    @abstractmethod
    def channel_names(self) -> List[str]: ...
    @abstractmethod
    def get_channel_buffers(self) -> List[List[float]]: ...
    @abstractmethod
    def set_channel_mute(self, channel_index: int, muted: bool) -> None: ...


class MockVgmEngine(VgmEngine):
    def __init__(self):
        self.current_path = None
        self.title = ""
        self.playing = False
        self.volume = 0.8
        self.channel_count = 10
        self.muted = [False] * self.channel_count
        self.frequency = [220.0, 110.0, 440.0, 880.0, 55.0, 330.0, 660.0, 165.0, 275.0, 385.0]
        self.sample_rate = 48000
        self.buffer_size = 512

        self.names = [
            "YM2612 FM 1 — lead",
            "YM2612 FM 2 — bass",
            "YM2612 FM 3 — chords",
            "YM2612 FM 4 — effects",
            "YM2612 FM 5 — extra",
            "YM2612 FM 6 — extra",
            "SN76489 PSG 1 — tone",
            "SN76489 PSG 2 — tone",
            "SN76489 PSG 3 — tone",
            "SN76489 PSG 4 — noise / drums",
        ]

    def load(self, path: str) -> None:
        self.current_path = Path(path)
        self.title = self.current_path.stem
        self.playing = False

    def play(self) -> None: self.playing = True
    def pause(self) -> None: self.playing = False

    def stop(self) -> None:
        self.playing = False

    def toggle(self) -> None: self.playing = not self.playing

    def set_volume(self, value: int) -> None: self.volume = value / 100.0

    def channel_names(self) -> List[str]: return list(self.names)

    def get_channel_buffers(self) -> List[List[float]]:
        if not self.playing:
            return [[] for _ in range(self.channel_count)]

        now = time.perf_counter()
        buffers = []

        for channel in range(self.channel_count):
            data = []
            for i in range(self.buffer_size):
                t = now + i / self.sample_rate
                frequency = self.frequency[channel]

                if channel == 9:
                    value = random.uniform(-0.7, 0.7)
                elif channel == 4:
                    value = math.sin(t * frequency * 2 * math.pi) * 0.5
                    value += math.sin(t * frequency * 4 * math.pi) * 0.25
                elif channel == 5:
                    value = math.tanh(math.sin(t * frequency * 2 * math.pi) * 3)
                else:
                    value = math.sin(t * frequency * 2 * math.pi)

                if self.muted[channel]:
                    value = 0.0

                data.append(value * self.volume)

            buffers.append(data)

        return buffers

    def set_channel_mute(self, channel_index: int, muted: bool) -> None:
        if 0 <= channel_index < self.channel_count:
            self.muted[channel_index] = muted
