#ifndef LIBVGM_BRIDGE_H
#define LIBVGM_BRIDGE_H

#include <memory>
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
    VgmBridge();
    ~VgmBridge();

    int open(const char* path);
    int play();
    int pause();
    int stop();

    std::vector<std::string> channel_names() const;
    size_t channel_count() const;

    int render_mix(int frame_count, float* out_samples);
    int render_channel(int channel_index, int frame_count, float* out_samples);

    int set_channel_mute(int channel_index, bool muted);
    int set_volume(float volume);

private:
    struct IsolatedRenderer;

    bool create_player(PlayerA** player, FileLoader** loader);
    int configure_player(PlayerA* player, FileLoader* loader, const char* path);
    int apply_channel_isolation(PlayerA* player, UINT32 target_device, UINT32 target_channel);

    PlayerA* mix_player_ = nullptr;
    FileLoader* mix_loader_ = nullptr;

    std::vector<std::unique_ptr<IsolatedRenderer>> renderers_;
    std::vector<std::string> names_;
    std::vector<bool> muted_;
    float volume_ = 1.0f;
    bool playing_ = false;
};

#endif
