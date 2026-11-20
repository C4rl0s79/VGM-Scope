#include "libvgm_bridge.h"

#include "player/playera.hpp"
#include "player/vgmplayer.hpp"
#include "DataLoader.h"

#include <algorithm>
#include <cstring>
#include <memory>
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
        delete static_cast<PlayerA*>(mix_player_);
        mix_player_ = nullptr;
    }

    if (mix_loader_) {
        FileLoader_Close(mix_loader_);
        FileLoader_Free(mix_loader_);
        mix_loader_ = nullptr;
    }
}

bool VgmBridge::create_player(void** player, void** loader)
{
    auto* new_player = new PlayerA();
    auto* new_loader = FileLoader_Create();

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

int VgmBridge::configure_player(void* player, const char* path)
{
    auto* plr = static_cast<PlayerA*>(player);
    auto* loader = static_cast<FileLoader*>(mix_loader_);

    if (!plr || !loader) return VGM_ERR_INTERNAL;

    if (!FileLoader_LoadFile(loader, path)) {
        return VGM_ERR_FILE_NOT_FOUND;
    }

    PlayerConfig cfg;
    plr->GetConfiguration(&cfg);
    cfg.masterVol = 1.0f;
    cfg.loopCount = 0;
    cfg.fadeTime = 0.0;
    plr->SetConfiguration(&cfg);

    if (!plr->LoadFile(loader)) {
        return VGM_ERR_UNSUPPORTED_FILE;
    }

    return VGM_OK;
}

int VgmBridge::apply_global_mute(void* player, int target_device, int target_channel, bool isolate)
{
    auto* plr = static_cast<PlayerA*>(player);
    std::vector<PLR_DEV_INFO> devices;
    plr->GetDeviceInfos(devices);

    for (const auto& device : devices) {
        PLR_DEV_OPTS opts;
        plr->GetDeviceOptions(device.id, &opts);

        if (!isolate) {
            opts.muteOpts.chnMute[0] = 0x00000000;
            opts.muteOpts.chnMute[1] = 0x00000000;
        } else if (static_cast<int>(device.id) == target_device) {
            UINT32 mask = 0xFFFFFFFFu;
            mask &= ~(1u << target_channel);
            opts.muteOpts.chnMute[0] = mask;
            opts.muteOpts.chnMute[1] = 0xFFFFFFFFu;
        } else {
            opts.muteOpts.chnMute[0] = 0xFFFFFFFFu;
            opts.muteOpts.chnMute[1] = 0xFFFFFFFFu;
        }

        plr->SetDeviceOptions(device.id, &opts);
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

    int result = configure_player(mix_player_, path);
    if (result != VGM_OK) return result;

    auto* mix = static_cast<PlayerA*>(mix_player_);
    std::vector<PLR_DEV_INFO> devices;
    mix->GetDeviceInfos(devices);

    UINT32 global_channel = 0;

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

            void* new_player = nullptr;
            void* new_loader = nullptr;

            if (!create_player(&new_player, &new_loader)) {
                return VGM_ERR_INTERNAL;
            }

            renderer->player = *static_cast<PlayerA*>(new_player);
            renderer->loader = static_cast<FileLoader*>(new_loader);
            renderer->device_id = device.id;
            renderer->local_channel = local;

            delete static_cast<PlayerA*>(new_player);

            if (!FileLoader_LoadFile(renderer->loader, path)) {
                return VGM_ERR_FILE_NOT_FOUND;
            }

            PlayerConfig cfg;
            renderer->player.GetConfiguration(&cfg);
            cfg.masterVol = 1.0f;
            cfg.loopCount = 0;
            cfg.fadeTime = 0.0;
            renderer->player.SetConfiguration(&cfg);

            if (!renderer->player.LoadFile(renderer->loader)) {
                return VGM_ERR_UNSUPPORTED_FILE;
            }

            apply_global_mute(
                &renderer->player,
                static_cast<int>(device.id),
                static_cast<int>(local),
                true
            );

            renderers_.push_back(std::move(renderer));
            ++global_channel;
        }
    }

    muted_.assign(names_.size(), false);
    return VGM_OK;
}

int VgmBridge::play()
{
    if (!mix_player_) return VGM_ERR_INTERNAL;

    auto* mix = static_cast<PlayerA*>(mix_player_);
    mix->Start();

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
        static_cast<PlayerA*>(mix_player_)->Stop();
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
    auto* mix = static_cast<PlayerA*>(mix_player_);

    UINT32 rendered = mix->Render(
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

    if (!mix_player_) return VGM_ERR_INTERNAL;

    auto* mix = static_cast<PlayerA*>(mix_player_);
    std::vector<PLR_DEV_INFO> devices;
    mix->GetDeviceInfos(devices);

    // MVP: mute jest stosowane do izolowanych rendererów przez GUI.
    // Główny miks pozostaje niezmieniony, aby nie zaburzać synchronizacji.

    return VGM_OK;
}

int VgmBridge::set_volume(float volume)
{
    volume_ = std::clamp(volume, 0.0f, 2.0f);
    return VGM_OK;
}
