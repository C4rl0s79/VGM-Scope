# Budowanie vgm_engine.dll

## Wymagania

- Windows 11
- Visual Studio 2022 z workloadem **Desktop development with C++**
- CMake 3.20+
- Git
- Internet przy pierwszej kompilacji — CMake pobierze libvgm z GitHuba

## Kompilacja

```bat
cmake -S backend -B backend\build -G "Visual Studio 17 2022" -A x64
cmake --build backend\build --config Release
```

Wynik:

```text
backend\build\Release\vgm_engine.dll
```

## Status

`vgm_engine.dll` ma już kompletne API C oraz gotowe wiązanie Python przez `ctypes`.

`libvgm_bridge.cpp` zawiera miejsca integracji z libvgm, ale wymaga dopasowania
 do konkretnej wersji źródeł libvgm, ponieważ jej wewnętrzne API może się zmieniać.

Do implementacji zostały:

1. Wczytanie pliku przez loader libvgm.
2. Utworzenie playera VGM.
3. Wykrycie chipów i globalna lista kanałów.
4. Osobne bufory PCM per kanał.
5. Mute/solo.
6. Wyjście audio WASAPI.
