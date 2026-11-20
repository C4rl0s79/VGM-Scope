#include "libvgm_bridge.h"

#include "player/vgmplayer.hpp"
#include "utils/DataLoader.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace {

constexpr UINT32 SAMPLE_RATE = 48000;
constexpr UINT32 CHANNELS = 2;
constexpr UINT32 BIT_DEPTH = 16;
constexpr UINT32 BUFFER_SIZE = 4096;
constexpr INT32 MASTER_VOLUME_1_0 = 65536;

struct IsolatedRenderer {
    std::unique_ptr<PlayerA> player;
    DATA_LOADER* loader = nullptr;
    UINT32 device_id = 0;
    UINT32 local_channel = 0;
};

float int16_to_float(INT16 value)
{
    return static_cast<float>(value) / 32768.0f;
}

}

VgmBridge::VgmBridge() = default;

VgmBridge::~VgmBridge()
{
    stop();
    renderers_.clear();

    if (mix_loader_) {
        FileLoader_Deinit(mix_loader_);
        mix_loader_ = nullptr;
    }
}

std::unique_ptr<PlayerA> VgmBridge::create_player()
{
    auto player = std::make_unique<PlayerA>();
    player->RegisterPlayerEngine(new VGMPlayer());
    player->SetOutputSettings(SAMPLE_RATE, CHANNELS, BIT_DEPTH, BUFFER_SIZE);
    return player;
}

int VgmBridge::configure_player(PlayerA& player, const char* path, DATA_LOADER** loader)
{
    if (!path || !loader) return VGM_ERR_INVALID_ARGUMENT;

    PlayerA::Config config = player.GetConfiguration();
    config.masterVol = MASTER_VOLUME_1_0;
    config.loopCount = 0;
    config.fadeSmpls = 0;
    config.endSilenceSmpls = 0;
    player.SetConfiguration(config);

    DATA_LOADER* new_loader = FileLoader_Init(path);

    if (!new_loader) {
        return VGM_ERR_FILE_NOT_FOUND;
    }

    UINT8 result = player.LoadFile(new_loader);

    if (result & 0x80) {
        FileLoader_Deinit(new_loader);
        return VGM_ERR_UNSUPPORTED_FILE;
    }

    *loader = new_loader;
    return VGM_OK;
}

int VgmBridge::apply_channel_isolation(PlayerA& player, UINT32 target_device, UINT32 target_channel)
{
    PlayerBase* base_player = player.GetPlayer();

    if (!base_player) return VGM_ERR_INTERNAL;

    std::vector<PLR_DEV_INFO> devices;
    base_player->GetSongDeviceInfo(devices);

    for (const auto& device : devices) {
        PLR_MUTE_OPTS mute_opts;
        base_player->GetDeviceMuting(device.id, mute_opts);

        UINT32 channel_count = 0;

        if (device.devDecl && device.devDecl->channelCount) {
            channel_count = device.devDecl->channelCount(device.devCfg);
        }

        UINT32 mask = 0;

        for (UINT32 channel = 0; channel < channel_count; ++channel) {
            if (device.id == target_device && channel == target_channel) {
                continue;
            }

            mask |= (1u << channel);
        }

        mute_opts.chnMute[0] = mask;
        base_player->SetDeviceMuting(device.id, mute_opts);
    }

    return VGM_OK;
}

int VgmBridge::open(const char* path)
{
    if (!path) return VGM_ERR_INVALID_ARGUMENT;

    stop();
    renderers_.clear();
    names_.clear();

    mix_player_ = create_player();

    int result = configure_player(*mix_player_, path, &mix_loader_);
    if (result != VGM_OK) return result;

    PlayerBase* base_player = mix_player_->GetPlayer();

    if (!base_player) return VGM_ERR_INTERNAL;

    std::vector<PLR_DEV_INFO> devices;
    base_player->GetSongDeviceInfo(devices);

    for (const auto& device : devices) {
        UINT32 channel_count = 0;

        if (device.devDecl && device.devDecl->channelCount) {
            channel_count = device.devDecl->channelCount(device.devCfg);
        }

        const char* const* device_channel_names = nullptr;

        if (device.devDecl && device.devDecl->channelNames) {
            device_channel_names = device.devDecl->channelNames(device.devCfg);
        }

        std::string device_name = "Sound device";

        if (device.devDecl && device.devDecl->name) {
            const char* name = device.devDecl->name(device.devCfg);
            if (name) device_name = name;
        }

        for (UINT32 local = 0; local < channel_count; ++local) {
            std::string channel_name;

            if (device_channel_names && device_channel_names[local]) {
                channel_name = device_channel_names[local];
            } else {
                channel_name = device_name + " CH " + std::to_string(local + 1);
            }

            names_.push_back(channel_name);

            auto renderer = std::make_unique<IsolatedRenderer>();
            renderer->player = create_player();

            result = configure_player(
                *renderer->player,
                path,
                &renderer->loader
            );

            if (result != VGM_OK) return result;

            renderer->device_id = device.id;
            renderer->local_channel = local;

            result = apply_channel_isolation(
                *renderer->player,
                device.id,
                local
            );

            if (result != VGM_OK) return result;

            renderers_.push_back(std::move(renderer));
        }
    }

    muted_.assign(names_.size(), false);
    return VGM_OK;
}

int VgmBridge::play()
{
    if (!mix_player_) return VGM_ERR_INTERNAL;

    mix_player_->Start();

    for (auto& renderer : renderers_) {
        renderer->player->Start();
    }

    playing_ = true;
    return VGM_OK;
}

int VgmBridge::pause()
{
    playing_ = false;
    return VGM_OK;
}

int VgmBridge::stop()
{
    playing_ = false;

    if (mix_player_) {
        mix_player_->Stop();
    }

    for (auto& renderer : renderers_) {
        renderer->player->Stop();
    }

    return VGM_OK;
}

std::vector<std::string> VgmBridge::channel_names() const
{
    return names_;
}

size_t VgmBridge::channel_count() const
{
    return names_.size();
}

int VgmBridge::render_mix(int frame_count, float* out_samples)
{
    if (!mix_player_ || !out_samples || frame_count <= 0) {
        return VGM_ERR_INVALID_ARGUMENT;
    }

    if (!playing_) {
        std::fill(out_samples, out_samples + static_cast<size_t>(frame_count) * CHANNELS, 0.0f);
        return VGM_OK;
    }

    std::vector<INT16> packed(static_cast<size_t>(frame_count) * CHANNELS);

    UINT32 rendered = mix_player_->Render(
        static_cast<UINT32>(frame_count) * CHANNELS * sizeof(INT16),
        packed.data()
    );

    size_t available_frames = rendered / (CHANNELS * sizeof(INT16));

    for (int frame = 0; frame < frame_count; ++frame) {
        if (static_cast<size_t>(frame) >= available_frames) {
            out_samples[frame * 2 + 0] = 0.0f;
            out_samples[frame * 2 + 1] = 0.0f;
            continue;
        }

        out_samples[frame * 2 + 0] = int16_to_float(packed[frame * 2 + 0]) * volume_;
        out_samples[frame * 2 + 1] = int16_to_float(packed[frame * 2 + 1]) * volume_;
    }

    return VGM_OK;
}

int VgmBridge::render_channel(int channel_index, int frame_count, float* out_samples)
{
    if (!out_samples || frame_count <= 0) {
        return VGM_ERR_INVALID_ARGUMENT;
    }

    if (channel_index < 0 || channel_index >= static_cast<int>(renderers_.size())) {
        return VGM_ERR_INVALID_CHANNEL;
    }

    if (!playing_) {
        std::fill(out_samples, out_samples + frame_count, 0.0f);
        return VGM_OK;
    }

    auto& renderer = renderers_[static_cast<size_t>(channel_index)];
    std::vector<INT16> packed(static_cast<size_t>(frame_count) * CHANNELS);

    UINT32 rendered = renderer->player->Render(
        static_cast<UINT32>(frame_count) * CHANNELS * sizeof(INT16),
        packed.data()
    );

    size_t available_frames = rendered / (CHANNELS * sizeof(INT16));

    for (int frame = 0; frame < frame_count; ++frame) {
        if (static_cast<size_t>(frame) >= available_frames) {
            out_samples[frame] = 0.0f;
            continue;
        }

        float left = int16_to_float(packed[frame * 2 + 0]);
        float right = int16_to_float(packed[frame * 2 + 1]);
        out_samples[frame] = ((left + right) * 0.5f) * volume_;
    }

    return VGM_OK;
}

int VgmBridge::set_channel_mute(int channel_index, bool muted)
{
    if (channel_index < 0 || channel_index >= static_cast<int>(muted_.size())) {
        return VGM_ERR_INVALID_CHANNEL;
    }

    muted_[static_cast<size_t>(channel_index)] = muted;
    return VGM_OK;
}

int VgmBridge::set_volume(float volume)
{
    volume_ = std::clamp(volume, 0.0f, 2.0f);
    return VGM_OK;
}
