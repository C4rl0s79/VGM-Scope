#include "vgm_engine.h"
#include "libvgm_bridge.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {
struct EngineState {
    std::unique_ptr<VgmBridge> bridge;
    std::vector<std::string> channel_names;
    bool playing = false;
    float volume = 1.0f;
};

EngineState* as_engine(VgmHandle handle) { return static_cast<EngineState*>(handle); }
}

extern "C" {

VGM_API int vgm_open(const char* path, VgmHandle* out_handle) {
    if (!path || !out_handle) return VGM_ERR_INVALID_ARGUMENT;

    try {
        auto bridge = std::make_unique<VgmBridge>();
        int result = bridge->open(path);
        if (result != VGM_OK) return result;

        auto state = std::make_unique<EngineState>();
        state->bridge = std::move(bridge);
        state->channel_names = state->bridge->channel_names();

        *out_handle = static_cast<VgmHandle>(state.release());
        return VGM_OK;
    } catch (...) {
        return VGM_ERR_INTERNAL;
    }
}

VGM_API void vgm_close(VgmHandle handle) { delete as_engine(handle); }

VGM_API int vgm_play(VgmHandle handle) {
    auto* state = as_engine(handle);
    if (!state) return VGM_ERR_INVALID_HANDLE;
    state->playing = true;
    return state->bridge->play();
}

VGM_API int vgm_pause(VgmHandle handle) {
    auto* state = as_engine(handle);
    if (!state) return VGM_ERR_INVALID_HANDLE;
    state->playing = false;
    return state->bridge->pause();
}

VGM_API int vgm_stop(VgmHandle handle) {
    auto* state = as_engine(handle);
    if (!state) return VGM_ERR_INVALID_HANDLE;
    state->playing = false;
    return state->bridge->stop();
}

VGM_API int vgm_is_playing(VgmHandle handle) {
    auto* state = as_engine(handle);
    return state && state->playing ? 1 : 0;
}

VGM_API int vgm_get_channel_count(VgmHandle handle) {
    auto* state = as_engine(handle);
    if (!state) return VGM_ERR_INVALID_HANDLE;
    return static_cast<int>(state->bridge->channel_count());
}

VGM_API int vgm_get_channel_name(VgmHandle handle, int channel_index, char* out_buffer, int buffer_size) {
    auto* state = as_engine(handle);
    if (!state) return VGM_ERR_INVALID_HANDLE;

    auto names = state->bridge->channel_names();

    if (channel_index < 0 || channel_index >= static_cast<int>(names.size()))
        return VGM_ERR_INVALID_CHANNEL;

    if (!out_buffer || buffer_size <= 0)
        return VGM_ERR_INVALID_ARGUMENT;

    const std::string& name = names[static_cast<size_t>(channel_index)];
    size_t length = std::min(name.size(), static_cast<size_t>(buffer_size - 1));

    std::memcpy(out_buffer, name.c_str(), length);
    out_buffer[length] = '\0';

    return VGM_OK;
}

VGM_API int vgm_get_mix_samples(VgmHandle handle, float* out_samples, int frame_count) {
    auto* state = as_engine(handle);
    if (!state) return VGM_ERR_INVALID_HANDLE;
    return state->bridge->render_mix(frame_count, out_samples);
}

VGM_API int vgm_get_channel_samples(VgmHandle handle, int channel_index, float* out_samples, int frame_count) {
    auto* state = as_engine(handle);
    if (!state) return VGM_ERR_INVALID_HANDLE;
    return state->bridge->render_channel(channel_index, frame_count, out_samples);
}

VGM_API int vgm_set_channel_mute(VgmHandle handle, int channel_index, int muted) {
    auto* state = as_engine(handle);
    if (!state) return VGM_ERR_INVALID_HANDLE;
    return state->bridge->set_channel_mute(channel_index, muted != 0);
}

VGM_API int vgm_set_volume(VgmHandle handle, float volume) {
    auto* state = as_engine(handle);
    if (!state) return VGM_ERR_INVALID_HANDLE;
    return state->bridge->set_volume(volume);
}

}
