#include "libvgm_bridge.h"
#include "loader_helpers.h"
#include "player/vgmplayer.hpp"
#include <algorithm>
#include <stdexcept>

VgmBridge::VgmBridge() = default;
VgmBridge::~VgmBridge() {
    stop();
    for (auto& r : renderers_) r->player.reset();
    renderers_.clear();
    mix_player_.reset();
    if (mix_loader_) FileLoader_Deinit(mix_loader_);
}
std::unique_ptr<PlayerA> VgmBridge::create_player() {
    auto p = std::make_unique<PlayerA>();
    p->RegisterPlayerEngine(new VGMPlayer());
    if (p->SetOutputSettings(48000, 2, 16, 4096)) throw std::runtime_error("Output settings failed");
    return p;
}
int VgmBridge::configure_player(PlayerA& p, const char* path, DATA_LOADER** loader) {
    auto cfg = p.GetConfiguration();
    cfg.masterVol = 65536; cfg.loopCount = 0; cfg.fadeSmpls = 0; cfg.endSilenceSmpls = 0; cfg.pbSpeed = 1.0;
    p.SetConfiguration(cfg);
    int rc = scope_load_file(p, path, loader);
    if (rc) return rc;
    rc = p.Start();
    return rc ? -1000-rc : 0;
}
int VgmBridge::apply_channel_isolation(PlayerA& p, UINT32 target, UINT32 encoded) {
    auto* b = p.GetPlayer();
    if (!b || encoded >= 64) return -6;
    std::vector<PLR_DEV_INFO> devices;
    b->GetSongDeviceInfo(devices);
    for (const auto& d : devices) {
        if (d.parentIdx != static_cast<UINT32>(-1)) continue;
        PLR_MUTE_OPTS m{};
        m.chnMute[0] = m.chnMute[1] = 0xFFFFFFFFu;
        if (d.id == target) m.chnMute[encoded/32] &= ~(1u << (encoded%32));
        int rc = b->SetDeviceMuting(d.id, m);
        if (rc) return -2000-rc;
    }
    return 0;
}
int VgmBridge::open(const char* path) {
    if (!path) return -1;
    stop();
    for (auto& r : renderers_) r->player.reset();
    renderers_.clear(); names_.clear();
    mix_player_.reset();
    if (mix_loader_) { FileLoader_Deinit(mix_loader_); mix_loader_ = nullptr; }
    mix_player_ = create_player();
    int rc = configure_player(*mix_player_, path, &mix_loader_);
    if (rc) return rc;
    std::vector<PLR_DEV_INFO> devices;
    rc = mix_player_->GetPlayer()->GetSongDeviceInfo(devices);
    if (rc) return -3000-rc;
    for (size_t i=0; i<devices.size(); ++i) {
        const auto& d = devices[i];
        if (!d.devDecl || !d.devDecl->channelCount) continue;
        UINT32 count = d.devDecl->channelCount(d.devCfg);
        if (count > 32) return -7;
        UINT32 root = d.id, slot = 0;
        if (d.parentIdx != static_cast<UINT32>(-1)) {
            if (d.parentIdx >= devices.size()) return -6;
            root = devices[d.parentIdx].id; slot = 1;
        }
        const char** cn = d.devDecl->channelNames ? d.devDecl->channelNames(d.devCfg) : nullptr;
        const char* dn = d.devDecl->name ? d.devDecl->name(d.devCfg) : nullptr;
        std::string prefix = std::string(dn ? dn : "Device") + " #" + std::to_string(d.instance+1);
        for (UINT32 c=0; c<count; ++c) {
            auto r = std::make_unique<IsolatedRenderer>();
            r->player = create_player();
            rc = configure_player(*r->player, path, &r->loader);
            if (rc) { r->player.reset(); return rc; }
            r->device_id = root; r->local_channel = slot*32+c;
            rc = apply_channel_isolation(*r->player, root, r->local_channel);
            if (rc) { r->player.reset(); return rc; }
            names_.push_back(prefix + " / " + (cn && cn[c] ? std::string(cn[c]) : "CH " + std::to_string(c+1)));
            renderers_.push_back(std::move(r));
        }
    }
    muted_.assign(names_.size(), false);
    playing_ = false;
    return 0;
}
int VgmBridge::play() {
    if (!mix_player_ || !mix_player_->GetPlayer()) return -6;
    if (!(mix_player_->GetPlayer()->GetState() & PLAYSTATE_PLAY)) {
        int rc = mix_player_->Start();
        if (rc) return -1000-rc;
        for (auto& r : renderers_) {
            rc = r->player->Start();
            if (rc) return -1000-rc;
            rc = apply_channel_isolation(*r->player, r->device_id, r->local_channel);
            if (rc) return rc;
        }
        for (size_t i=0; i<muted_.size(); ++i) { rc = set_channel_mute(static_cast<int>(i), muted_[i]); if (rc) return rc; }
    }
    playing_ = true;
    return 0;
}
int VgmBridge::pause() { playing_ = false; return 0; }
int VgmBridge::stop() {
    playing_ = false;
    if (mix_player_) mix_player_->Stop();
    for (auto& r : renderers_) if (r->player) r->player->Stop();
    return 0;
}
std::vector<std::string> VgmBridge::channel_names() const { return names_; }
size_t VgmBridge::channel_count() const { return names_.size(); }
int VgmBridge::render_mix(int frames, float* out) {
    if (!mix_player_ || !out || frames<=0 || frames>4096) return -1;
    std::fill(out, out+static_cast<size_t>(frames)*2, 0.0f);
    if (!playing_) return 0;
    std::vector<INT16> data(static_cast<size_t>(frames)*2);
    auto bytes = mix_player_->Render(static_cast<UINT32>(frames)*4, data.data());
    size_t n = std::min(data.size(), static_cast<size_t>(bytes)/2);
    for (size_t i=0; i<n; ++i) out[i] = data[i]/32768.0f*volume_;
    return 0;
}
int VgmBridge::render_channel(int ch, int frames, float* out) {
    if (!out || frames<=0 || frames>4096) return -1;
    if (ch<0 || ch>=static_cast<int>(renderers_.size())) return -3;
    std::fill(out, out+frames, 0.0f);
    if (!playing_) return 0;
    std::vector<INT16> data(static_cast<size_t>(frames)*2);
    auto bytes = renderers_[ch]->player->Render(static_cast<UINT32>(frames)*4, data.data());
    size_t n = std::min(static_cast<size_t>(frames), static_cast<size_t>(bytes)/4);
    for (size_t i=0; i<n; ++i) out[i]=(static_cast<float>(data[i*2])+data[i*2+1])/65536.0f*volume_;
    return 0;
}
int VgmBridge::set_channel_mute(int ch, bool muted) {
    if (ch<0 || ch>=static_cast<int>(muted_.size())) return -3;
    auto* b = mix_player_->GetPlayer();
    auto& r = renderers_[ch];
    PLR_MUTE_OPTS m{};
    int rc = b->GetDeviceMuting(r->device_id, m);
    if (rc) return -2000-rc;
    auto& mask = m.chnMute[r->local_channel/32];
    UINT32 bit = 1u << (r->local_channel%32);
    if (muted) mask |= bit; else mask &= ~bit;
    rc = b->SetDeviceMuting(r->device_id, m);
    if (rc) return -2000-rc;
    muted_[ch] = muted;
    return 0;
}
int VgmBridge::set_volume(float v) { volume_=std::clamp(v,0.0f,2.0f); return 0; }
