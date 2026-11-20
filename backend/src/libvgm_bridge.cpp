#include "libvgm_bridge.h"
#include "loader_helpers.h"
#include "player/vgmplayer.hpp"
#include <algorithm>

VgmBridge::VgmBridge() = default;
VgmBridge::~VgmBridge() {
    stop();
    renderers_.clear();
    if (mix_player_) { mix_player_->UnloadFile(); mix_player_.reset(); }
    if (mix_loader_) FileLoader_Deinit(mix_loader_);
}
std::unique_ptr<PlayerA> VgmBridge::create_player() {
    auto p = std::make_unique<PlayerA>();
    p->RegisterPlayerEngine(new VGMPlayer());
    p->SetOutputSettings(48000, 2, 16, 4096);
    return p;
}
int VgmBridge::configure_player(PlayerA& p, const char* path, DATA_LOADER** loader) {
    auto cfg = p.GetConfiguration();
    cfg.masterVol = 65536;
    cfg.loopCount = 0;
    cfg.fadeSmpls = 0;
    cfg.endSilenceSmpls = 0;
    p.SetConfiguration(cfg);
    return scope_load_file(p, path, loader);
}
int VgmBridge::apply_channel_isolation(PlayerA& p, UINT32 target, UINT32 channel) {
    auto* b = p.GetPlayer();
    if (!b) return -6;
    std::vector<PLR_DEV_INFO> devices;
    b->GetSongDeviceInfo(devices);
    for (const auto& d : devices) {
        PLR_MUTE_OPTS m{};
        b->GetDeviceMuting(d.id, m);
        m.chnMute[0] = 0xFFFFFFFFu;
        m.chnMute[1] = 0xFFFFFFFFu;
        if (d.id == target && channel < 32) m.chnMute[0] &= ~(1u << channel);
        b->SetDeviceMuting(d.id, m);
    }
    return 0;
}
int VgmBridge::open(const char* path) {
    if (!path) return -1;
    stop();
    renderers_.clear();
    names_.clear();
    if (mix_player_) { mix_player_->UnloadFile(); mix_player_.reset(); }
    if (mix_loader_) { FileLoader_Deinit(mix_loader_); mix_loader_ = nullptr; }
    mix_player_ = create_player();
    int rc = configure_player(*mix_player_, path, &mix_loader_);
    if (rc) return rc;
    auto* b = mix_player_->GetPlayer();
    if (!b) return -6;
    std::vector<PLR_DEV_INFO> devices;
    b->GetSongDeviceInfo(devices);
    for (const auto& d : devices) {
        if (!d.devDecl || !d.devDecl->channelCount) continue;
        auto count = d.devDecl->channelCount(d.devCfg);
        const char** names = d.devDecl->channelNames ? d.devDecl->channelNames(d.devCfg) : nullptr;
        const char* name = d.devDecl->name ? d.devDecl->name(d.devCfg) : nullptr;
        for (UINT32 c = 0; c < count; ++c) {
            names_.push_back(names && names[c] ? names[c] : std::string(name ? name : "Device") + " CH " + std::to_string(c+1));
            auto r = std::make_unique<IsolatedRenderer>();
            r->player = create_player();
            rc = configure_player(*r->player, path, &r->loader);
            if (rc) return rc;
            r->device_id = d.id;
            r->local_channel = c;
            rc = apply_channel_isolation(*r->player, d.id, c);
            if (rc) return rc;
            renderers_.push_back(std::move(r));
        }
    }
    muted_.assign(names_.size(), false);
    return 0;
}
int VgmBridge::play() {
    if (!mix_player_) return -6;
    mix_player_->Start();
    for (auto& r : renderers_) r->player->Start();
    playing_ = true;
    return 0;
}
int VgmBridge::pause() { playing_ = false; return 0; }
int VgmBridge::stop() {
    playing_ = false;
    if (mix_player_) mix_player_->Stop();
    for (auto& r : renderers_) r->player->Stop();
    return 0;
}
std::vector<std::string> VgmBridge::channel_names() const { return names_; }
size_t VgmBridge::channel_count() const { return names_.size(); }
int VgmBridge::render_mix(int frames, float* out) {
    if (!mix_player_ || !out || frames <= 0) return -1;
    std::fill(out, out + static_cast<size_t>(frames)*2, 0.0f);
    if (!playing_) return 0;
    std::vector<INT16> data(static_cast<size_t>(frames)*2);
    auto bytes = mix_player_->Render(static_cast<UINT32>(frames)*4, data.data());
    size_t n = std::min(data.size(), static_cast<size_t>(bytes)/2);
    for (size_t i = 0; i < n; ++i) out[i] = data[i] / 32768.0f * volume_;
    return 0;
}
int VgmBridge::render_channel(int channel, int frames, float* out) {
    if (!out || frames <= 0) return -1;
    if (channel < 0 || channel >= static_cast<int>(renderers_.size())) return -3;
    std::fill(out, out + frames, 0.0f);
    if (!playing_) return 0;
    std::vector<INT16> data(static_cast<size_t>(frames)*2);
    auto bytes = renderers_[channel]->player->Render(static_cast<UINT32>(frames)*4, data.data());
    size_t n = std::min(static_cast<size_t>(frames), static_cast<size_t>(bytes)/4);
    for (size_t i = 0; i < n; ++i) out[i] = (static_cast<float>(data[i*2]) + data[i*2+1]) / 65536.0f * volume_;
    return 0;
}
int VgmBridge::set_channel_mute(int channel, bool muted) {
    if (channel < 0 || channel >= static_cast<int>(muted_.size())) return -3;
    muted_[channel] = muted;
    return 0;
}
int VgmBridge::set_volume(float v) { volume_ = std::clamp(v, 0.0f, 2.0f); return 0; }
