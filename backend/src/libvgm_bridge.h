#ifndef LIBVGM_BRIDGE_H
#define LIBVGM_BRIDGE_H

#include <string>
#include <vector>

enum {
    VGM_OK = 0,
    VGM_ERR_INVALID_ARGUMENT = -1,
    VGM_ERR_INVALID_HANDLE = -2,
    VGM_ERR_INVALID_CHANNEL = -3,
    VGM_ERR_FILE_NOT_FOUND = -4,
    VGM_ERR_UNSUPPORTED_FILE = -5,
    VGM_ERR_INTERNAL = -6
};

class VgmBridge {
public:
    VgmBridge() = default;
    ~VgmBridge();

    int open(const char* path);
    int play();
    int pause();
    int stop();

    std::vector<std::string> channel_names() const;

    int get_channel_samples(int channel_index, float* out_samples, int sample_count);
    int set_channel_mute(int channel_index, bool muted);
    int set_volume(float volume);

private:
    void* player_ = nullptr;
    std::vector<std::string> names_;
};

#endif
