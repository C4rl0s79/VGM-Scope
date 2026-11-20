# VGM Scope

Prosty odtwarzacz VGM/VGZ dla Windows 11 z osobnym oscyloskopem dla każdego kanału,
automatycznymi i edytowalnymi etykietami oraz mute/solo.

## Aktualny stan

GUI jest gotowe i działa z silnikiem testowym `MockVgmEngine`.

- Otwieranie pojedynczego pliku VGM/VGZ lub playlisty M3U.
- Play / Pause / Stop / poprzedni / następny.
- Siatka oscyloskopów per kanał.
- Automatyczne etykiety kanałów.
- Edycja etykiety przez dwuklik.
- Mute dla każdego kanału.
- Solo dla każdego kanału; ponowne kliknięcie **S** przywraca dźwięk wszystkich kanałów.
- Globalna numeracja kanałów dla plików VGM z kilkoma chipami dźwiękowymi.
- Głośność.
- Obsługa przeciągania pliku na okno.

## Uruchomienie GUI

```bat
py -3.11 -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt
python -m app.main
```

## Solo

- Pierwsze kliknięcie **S** wycisza wszystkie pozostałe kanały.
- Ponowne kliknięcie **S** przywraca dźwięk wszystkich kanałów.
- Solo nie nadpisuje trwale ustawień mute; po wyłączeniu solo wszystkie kanały są aktywne.

## Kilka chipów w jednym VGM

Plik VGM może zawierać więcej niż jeden chip dźwiękowy, np. YM2612 i SN76489.
Kanały są numerowane globalnie w całym pliku:

```text
0  YM2612 FM 1 — lead
1  YM2612 FM 2 — bass
2  YM2612 FM 3 — chords
3  YM2612 FM 4 — effects
4  SN76489 PSG 1 — tone
5  SN76489 PSG 2 — tone
6  SN76489 PSG 3 — tone
7  SN76489 PSG 4 — noise / drums
```

## Backend

Katalog `backend/` zawiera szkielet `vgm_engine.dll` dla libvgm oraz gotowe wiązanie
Python `ctypes` w `app/libvgm_engine.py`.

Instrukcja kompilacji: `backend/BUILD.md`.

```bat
cmake -S backend -B backend\build -G "Visual Studio 17 2022" -A x64
cmake --build backend\build --config Release
```

Po zbudowaniu DLL zmień w `app/main.py`:

```python
from app.engine import MockVgmEngine
```

na:

```python
from app.libvgm_engine import LibVgmEngine
```

oraz:

```python
self.engine = MockVgmEngine()
```

na:

```python
self.engine = LibVgmEngine(r"backend\build\Release\vgm_engine.dll")
```

## Format etykiet

```json
{
  "C:/Muzyka/VGM/gra.vgm": {
    "channels": {
      "0": "YM2612 FM 1 — lead",
      "1": "YM2612 FM 2 — bass",
      "4": "SN76489 PSG 1 — tone",
      "7": "SN76489 PSG 4 — noise / drums"
    }
  }
}
```
