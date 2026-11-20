from __future__ import annotations

import ctypes
import os
import sys
import threading
from pathlib import Path
from typing import List

import sounddevice as sd

from app.engine import VgmEngine


def _bundled_dll_path() -> Path:
    if getattr(sys, "frozen", False):
        base = Path(sys._MEIPASS)
    else:
        base = Path(__file__).resolve().parent.parent

    return base / "vgm_engine.dll"


class LibVgmEngine(VgmEngine):
    """Backend libvgm: realne odtwarzanie VGM/VGZ oraz osobne kanały."""

    def __init__(self, dll_path: str | Path | None = None):
        dll_path = Path(dll_path) if dll_path else _bundled_dll_path()
        self.lib = ctypes.WinDLL(str(dll_path))

        self._bind_functions()

        self.handle = None
        self.current_path = None
        self.title = ""
        self.channel_count = 0
        self.buffer_size = 512

        self._stream = None
        self._lock = threading.Lock()
        self._channel_buffers: List[List[float]] = []

    def _bind_functions(self):
        lib = self.lib

        lib.vgm_open.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
        lib.vgm_open.restype = ctypes.c_int

        lib.vgm_close.argtypes = [ctypes.c_void_p]
        lib.vgm_close.restype = None

        lib.vgm_play.argtypes = [ctypes.c_void_p]
        lib.vgm_play.restype = ctypes.c_int

        lib.vgm_pause.argtypes = [ctypes.c_void_p]
        lib.vgm_pause.restype = ctypes.c_int

        lib.vgm_stop.argtypes = [ctypes.c_void_p]
        lib.vgm_stop.restype = ctypes.c_int

        lib.vgm_get_channel_count.argtypes = [ctypes.c_void_p]
        lib.vgm_get_channel_count.restype = ctypes.c_int

        lib.vgm_get_channel_name.argtypes = [
            ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_int
        ]
        lib.vgm_get_channel_name.restype = ctypes.c_int

        lib.vgm_get_mix_samples.argtypes = [
            ctypes.c_void_p, ctypes.POINTER(ctypes.c_float), ctypes.c_int
        ]
        lib.vgm_get_mix_samples.restype = ctypes.c_int

        lib.vgm_get_channel_samples.argtypes = [
            ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_float), ctypes.c_int
        ]
        lib.vgm_get_channel_samples.restype = ctypes.c_int

        lib.vgm_set_channel_mute.argtypes = [
            ctypes.c_void_p, ctypes.c_int, ctypes.c_int
        ]
        lib.vgm_set_channel_mute.restype = ctypes.c_int

        lib.vgm_set_volume.argtypes = [ctypes.c_void_p, ctypes.c_float]
        lib.vgm_set_volume.restype = ctypes.c_int

    def load(self, path: str) -> None:
        with self._lock:
            if self.handle:
                self.lib.vgm_close(self.handle)
                self.handle = None

            handle = ctypes.c_void_p()
            result = self.lib.vgm_open(path.encode("utf-8"), ctypes.byref(handle))

            if result != 0:
                raise RuntimeError(f"libvgm: nie można otworzyć pliku, kod {result}")

            self.handle = handle
            self.current_path = Path(path)
            self.title = self.current_path.stem
            self.channel_count = self.lib.vgm_get_channel_count(self.handle)
            self._channel_buffers = [[] for _ in range(self.channel_count)]

    def play(self) -> None:
        if not self.handle:
            return

        self.lib.vgm_play(self.handle)

        if self._stream is None:
            self._stream = sd.OutputStream(
                samplerate=48000,
                channels=2,
                dtype="float32",
                callback=self._audio_callback,
            )
            self._stream.start()

    def pause(self) -> None:
        if self.handle:
            self.lib.vgm_pause(self.handle)

        if self._stream:
            self._stream.stop()
            self._stream.close()
            self._stream = None

    def stop(self) -> None:
        if self.handle:
            self.lib.vgm_stop(self.handle)

        if self._stream:
            self._stream.stop()
            self._stream.close()
            self._stream = None

    def toggle(self) -> None:
        pass

    def set_volume(self, value: int) -> None:
        if self.handle:
            self.lib.vgm_set_volume(self.handle, value / 100.0)

    def channel_names(self) -> List[str]:
        if not self.handle:
            return []

        names = []

        for index in range(self.channel_count):
            buffer = ctypes.create_string_buffer(256)
            result = self.lib.vgm_get_channel_name(self.handle, index, buffer, 256)

            if result == 0:
                names.append(buffer.value.decode("utf-8", errors="replace"))
            else:
                names.append(f"Channel {index}")

        return names

    def _audio_callback(self, outdata, frames, time_info, status):
        if not self.handle:
            outdata.fill(0)
            return

        mix = (ctypes.c_float * (frames * 2))()
        self.lib.vgm_get_mix_samples(self.handle, mix, frames)

        channel_cache = []

        for channel in range(self.channel_count):
            buffer = (ctypes.c_float * frames)()
            self.lib.vgm_get_channel_samples(
                self.handle, channel, buffer, frames
            )
            channel_cache.append(list(buffer))

        with self._lock:
            self._channel_buffers = channel_cache

        for frame in range(frames):
            outdata[frame, 0] = mix[frame * 2 + 0]
            outdata[frame, 1] = mix[frame * 2 + 1]

    def get_channel_buffers(self) -> List[List[float]]:
        with self._lock:
            return [list(channel) for channel in self._channel_buffers]

    def set_channel_mute(self, channel_index: int, muted: bool) -> None:
        if self.handle:
            self.lib.vgm_set_channel_mute(
                self.handle, channel_index, 1 if muted else 0
            )
