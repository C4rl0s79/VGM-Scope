#ifndef VGM_ENGINE_H
#define VGM_ENGINE_H

#include <stdint.h>

#if defined(_WIN32)
    #if defined(VGM_ENGINE_EXPORTS)
        #define VGM_API __declspec(dllexport)
    #else
        #define VGM_API __declspec(dllimport)
    #endif
#else
    #define VGM_API __attribute__((visibility("default")))
#endif

typedef void* VgmHandle;

extern "C" {

VGM_API int vgm_open(const char* path, VgmHandle* out_handle);
VGM_API void vgm_close(VgmHandle handle);

VGM_API int vgm_play(VgmHandle handle);
VGM_API int vgm_pause(VgmHandle handle);
VGM_API int vgm_stop(VgmHandle handle);
VGM_API int vgm_is_playing(VgmHandle handle);

VGM_API int vgm_get_channel_count(VgmHandle handle);
VGM_API int vgm_get_channel_name(VgmHandle handle, int channel_index, char* out_buffer, int buffer_size);

VGM_API int vgm_get_channel_samples(VgmHandle handle, int channel_index, float* out_samples, int sample_count);
VGM_API int vgm_set_channel_mute(VgmHandle handle, int channel_index, int muted);
VGM_API int vgm_set_volume(VgmHandle handle, float volume);

}

#endif
