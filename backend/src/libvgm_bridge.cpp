#include "libvgm_bridge.h"

#include <algorithm>

VgmBridge::~VgmBridge() {
    stop();
}

int VgmBridge::open(const char* path) {
    if (!path) return VGM_ERR_INVALID_ARGUMENT;

    // TODO: integracja z libvgm.
    //
    // 1. Wczytaj plik przez loader libvgm.
    // 2. Utwórz player VGM.
    // 3. Skonfiguruj 48000 Hz, stereo.
    // 4. Zainicjalizuj emulatory chipów wykryte w pliku VGM.
    // 5. Zbuduj jedną globalną listę kanałów ze wszystkich chipów.
    //
    // Przykład dla pliku Genesis:
    //   0 YM2612 FM 1
    //   1 YM2612 FM 2
    //   2 YM2612 FM 3
    //   3 YM2612 FM 4
    //   4 YM2612 FM 5
    //   5 YM2612 FM 6
    //   6 SN76489 PSG 1
    //   7 SN76489 PSG 2
    //   8 SN76489 PSG 3
    //   9 SN76489 PSG 4
    //
    // Ważne: kanały są globalne dla całego pliku, nawet jeśli pochodzą
    // z kilku różnych chipów.

    names_.clear();
    return VGM_ERR_UNSUPPORTED_FILE;
}

int VgmBridge::play() {
    return VGM_ERR_UNSUPPORTED_FILE;
}

int VgmBridge::pause() {
    return VGM_ERR_UNSUPPORTED_FILE;
}

int VgmBridge::stop() {
    return VGM_ERR_UNSUPPORTED_FILE;
}

std::vector<std::string> VgmBridge::channel_names() const {
    return names_;
}

int VgmBridge::get_channel_samples(int channel_index, float* out_samples, int sample_count) {
    if (!out_samples || sample_count <= 0)
        return VGM_ERR_INVALID_ARGUMENT;

    // TODO: pobierz próbki z bufora kanału.
    //
    // MVP: osobna instancja dekodera libvgm dla każdego kanału.
    // W danej instancji wycisz wszystkie kanały poza jednym.
    // Alternatywa: zmodyfikowana integracja libvgm z osobnymi buforami per kanał.

    std::fill(out_samples, out_samples + sample_count, 0.0f);
    return VGM_ERR_UNSUPPORTED_FILE;
}

int VgmBridge::set_channel_mute(int channel_index, bool muted) {
    return VGM_ERR_UNSUPPORTED_FILE;
}

int VgmBridge::set_volume(float volume) {
    return VGM_OK;
}
