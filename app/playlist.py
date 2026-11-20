from __future__ import annotations

from pathlib import Path
from typing import List


def load_playlist(path: Path) -> List[Path]:
    base = path.parent
    tracks: List[Path] = []

    for raw_line in path.read_text(encoding="utf-8-sig", errors="ignore").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue

        if line.startswith("file://"):
            line = line[7:]

        candidate = Path(line)
        if not candidate.is_absolute():
            candidate = base / candidate

        if candidate.exists():
            tracks.append(candidate.resolve())

    return tracks
