#include "libvgm_bridge.h"

#include "player/playera.hpp"
#include "player/vgmplayer.hpp"
#include "utils/DataLoader.h"
#include "utils/FileLoader.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace {

constexpr UINT32 SAMPLE_RATE = 48000;
constexpr UINT32 CHANNELS = 2;
constexpr UINT32 BIT_DEPTH = 16;
constexpr UINT32 BUFFER_SIZE = 4096;

struct IsolatedRenderer {
    PlayerA player;
    FileLoader* loader = nullptr;
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

    if (mix_player_) {
        delete mix_player_;
        mix_player_ = nullptr;
    }

    if (mix_loader_) {
        FileLoader_Free(mix_loader_);
        mix_loader_ = nullptr;
    }
}

bool VgmBridge::create_player(PlayerA** player, FileLoader** loader)
{
    auto* new_player = new PlayerA();
    auto* new_loader = FileLoader_Init(nullptr);

    if (!new_player || !new_loader) {
        delete new_player;
        if (new_loader) FileLoader_Free(new_loader);
        return false;
    }

    new_player->RegisterPlayerEngine(new VGMPlayer());
    new_player->SetOutputSettings(SAMPLE_RATE, CHANNELS, BIT_DEPTH, BUFFER_SIZE);

    *player = new_player;
    *loader = new_loader;
    return true;
}

int VgmBridge::configure_player(PlayerA* player, FileLoader* loader, const char* path)
{
    if (!player || !loader || !path) return VGM_ERR_INVALID_ARGUMENT;

    PlayerConfig cfg;
    player->GetConfiguration(&cfg);
    cfg.masterVol = 1.0f;
    cfg.loopCount = 0;
    cfg.fadeTime = 0.0;
    player->SetConfiguration(&cfg);

    if (!player->LoadFile(loader)) {
        return VGM_ERR_UNSUPPORTED_FILE;
    }

    return VGM_OK;
}

int VgmBridge::apply_channel_isolation(PlayerA* player, UINT32 target_device, UINT32 target_channel)
{
    if (!player) return VGM_ERR_INVALID_ARGUMENT;

    std::vector<PLR_DEV_INFO> devices;
    player->GetSongDeviceInfo(devices);

    for (const auto& device : devices) {
        PLR_MUTE_OPTS mute_opts;
        player->GetDeviceMuting(device.id, mute_opts);

        if (device.id == target_device) {
            UINT32 channel_count = 0;

            if (device.devDecl && device.devDecl->channelCount) {
                channel_count = device.devDecl->channelCount(device.devCfg);
            }

            UINT32 mask = 0;

            for (UINT32 channel = 0; channel < channel_count; ++channel) {
                if (channel != target_channel) {
                    mask |= (1u << channel);
                }
            }

            mute_opts.chnMute[0] = mask;
        } else {
            UINT32 channel_count = 0;

            if (device.devDecl && device.devDecl->channelCount) {
                channel_count = device.devDecl->channelCount(device.devCfg);
            }

            UINT32 mask = 0;

            for (UINT32 channel = 0; channel < channel_count; ++channel) {
                mask |= (1u << channel);
            }

            mute_opts.chnMute[0] = mask;
        }

        player->SetDeviceMuting(device.id, mute_opts);
    }

    return VGM_OK;
}

int VgmBridge::open(const char* path)
{
    if (!path) return VGM_ERR_INVALID_ARGUMENT;

    stop();
    renderers_.clear();
    names_.clear();

    if (!create_player(&mix_player_, &mix_loader_)) {
        return VGM_ERR_INTERNAL;
    }

    int result = configure_player(mix_player_, mix_loader_, path);
    if (result != VGM_OK) return result;

    std::vector<PLR_DEV_INFO> devices;
    mix_player_->GetSongDeviceInfo(devices);

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

            PlayerA* new_player = nullptr;
            FileLoader* new_loader = nullptr;

            if (!create_player(&new_player, &new_loader)) {
                return VGM_ERR_INTERNAL;
            }

            renderer->player = std::move(*new_player);
            renderer->loader = new_loader;
            renderer->device_id = device.id;
            renderer->local_channel = local;

            delete new_player;

            result = configure_player(&renderer->player, renderer->loader, path);
            if (result != VGM_OK) return result;

            apply_channel_isolation(
                &renderer->player,
                device.id,
                local
            );

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
        renderer->player.Start();
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
        renderer->player.Stop();
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

    UINT32 rendered = renderer->player.Render(
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
