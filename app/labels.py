from __future__ import annotations

import json
from pathlib import Path
from typing import Dict


class LabelStore:
    def __init__(self, path: Path):
        self.path = path
        self.data: Dict[str, Dict[str, str]] = {}
        self._load()

    def _load(self) -> None:
        if self.path.exists():
            try:
                self.data = json.loads(self.path.read_text(encoding="utf-8"))
            except (json.JSONDecodeError, OSError):
                self.data = {}

    def get_labels(self, song_path: str) -> Dict[str, str]:
        return self.data.get(song_path, {}).get("channels", {})

    def set_label(self, song_path: str, channel_index: int, label: str) -> None:
        song = self.data.setdefault(song_path, {})
        channels = song.setdefault("channels", {})
        channels[str(channel_index)] = label
        self._save()

    def _save(self) -> None:
        self.path.write_text(
            json.dumps(self.data, ensure_ascii=False, indent=2),
            encoding="utf-8"
        )
