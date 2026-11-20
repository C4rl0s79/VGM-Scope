from __future__ import annotations

import ctypes
from pathlib import Path
from typing import List

from app.engine import VgmEngine


class LibVgmEngine(VgmEngine):
    """Backend dla skompilowanego vgm_engine.dll opartego o libvgm."""

    def __init__(self, dll_path: str | Path):
        self.lib = ctypes.WinDLL(str(dll_path))

        self.lib.vgm_open.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
        self.lib.vgm_open.restype = ctypes.c_int

        self.lib.vgm_close.argtypes = [ctypes.c_void_p]
        self.lib.vgm_close.restype = None

        self.lib.vgm_play.argtypes = [ctypes.c_void_p]
        self.lib.vgm_play.restype = ctypes.c_int

        self.lib.vgm_pause.argtypes = [ctypes.c_void_p]
        self.lib.vgm_pause.restype = ctypes.c_int

        self.lib.vgm_stop.argtypes = [ctypes.c_void_p]
        self.lib.vgm_stop.restype = ctypes.c_int

        self.lib.vgm_get_channel_count.argtypes = [ctypes.c_void_p]
        self.lib.vgm_get_channel_count.restype = ctypes.c_int

        self.lib.vgm_get_channel_name.argtypes = [
            ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_int
        ]
        self.lib.vgm_get_channel_name.restype = ctypes.c_int

        self.lib.vgm_get_channel_samples.argtypes = [
            ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_float), ctypes.c_int
        ]
        self.lib.vgm_get_channel_samples.restype = ctypes.c_int

        self.lib.vgm_set_channel_mute.argtypes = [
            ctypes.c_void_p, ctypes.c_int, ctypes.c_int
        ]
        self.lib.vgm_set_channel_mute.restype = ctypes.c_int

        self.lib.vgm_set_volume.argtypes = [ctypes.c_void_p, ctypes.c_float]
        self.lib.vgm_set_volume.restype = ctypes.c_int

        self.handle = None
        self.current_path = None
        self.title = ""
        self.channel_count = 0
        self.buffer_size = 512

    def load(self, path: str) -> None:
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

    def play(self) -> None:
        if self.handle: self.lib.vgm_play(self.handle)

    def pause(self) -> None:
        if self.handle: self.lib.vgm_pause(self.handle)

    def stop(self) -> None:
        if self.handle: self.lib.vgm_stop(self.handle)

    def toggle(self) -> None:
        pass

    def set_volume(self, value: int) -> None:
        if self.handle: self.lib.vgm_set_volume(self.handle, value / 100.0)

    def channel_names(self) -> List[str]:
        names = []

        for index in range(self.channel_count):
            buffer = ctypes.create_string_buffer(256)
            result = self.lib.vgm_get_channel_name(self.handle, index, buffer, 256)

            if result == 0:
                names.append(buffer.value.decode("utf-8", errors="replace"))
            else:
                names.append(f"Channel {index}")

        return names

    def get_channel_buffers(self) -> List[List[float]]:
        if not self.handle:
            return []

        buffers = []

        for channel in range(self.channel_count):
            buffer = (ctypes.c_float * self.buffer_size)()
            result = self.lib.vgm_get_channel_samples(
                self.handle, channel, buffer, self.buffer_size
            )

            if result == 0:
                buffers.append(list(buffer))
            else:
                buffers.append([0.0] * self.buffer_size)

        return buffers

    def set_channel_mute(self, channel_index: int, muted: bool) -> None:
        if self.handle:
            self.lib.vgm_set_channel_mute(self.handle, channel_index, 1 if muted else 0)
