// Procedural client-side audio. miniaudio drives the device; every clip is
// synthesised into a mono f32 PCM buffer at init (no asset files). One-shots
// play through a small pool of voices (each a lightweight ma_audio_buffer_ref
// over the shared PCM + a ma_sound), attenuated and panned in software so we
// stay independent of miniaudio's 3D model. Two looping beds — wind and hearth
// fire — have their volume eased each frame from weather and hearth proximity.
#include "audio.h"
#include "miniaudio.h"

#include <vector>
#include <cmath>
#include <random>
#include <algorithm>

AudioSystem* g_audio = nullptr;

namespace {
constexpr int   SR   = 48000;            // synthesis + engine sample rate
constexpr float PI2  = 6.28318530718f;
constexpr int   MAX_VOICES = 24;         // simultaneous one-shots

std::mt19937 g_rng(0xA0D10C0Du);
inline float nz() {                       // white noise in [-1, 1]
    std::uniform_real_distribution<float> d(-1.0f, 1.0f);
    return d(g_rng);
}
inline float u01() {
    std::uniform_real_distribution<float> d(0.0f, 1.0f);
    return d(g_rng);
}

// One-pole low-pass smoothing in place; `a` in (0,1], smaller = darker.
void lowpass(std::vector<float>& b, float a) {
    float y = 0.0f;
    for (float& s : b) { y += a * (s - y); s = y; }
}
void normalize(std::vector<float>& b, float peak) {
    float m = 1e-6f;
    for (float s : b) m = std::max(m, std::fabs(s));
    float g = peak / m;
    for (float& s : b) s *= g;
}
// Half-sine "rise then fall" envelope over the clip (u in 0..1).
inline float swell(float u) { return std::sin(PI2 * 0.5f * u); }

std::vector<float> genFootstep() {
    int N = (int)(SR * 0.15f); std::vector<float> b(N);
    for (int i = 0; i < N; i++) {
        float t = (float)i / SR;
        float env   = std::exp(-t * 26.0f);
        float thump = std::sin(PI2 * 68.0f * t) * std::exp(-t * 34.0f);
        b[i] = (nz() * 0.55f + thump * 0.9f) * env;
    }
    lowpass(b, 0.18f); normalize(b, 0.5f); return b;
}
std::vector<float> genBlockBreak() {
    int N = (int)(SR * 0.18f); std::vector<float> b(N);
    for (int i = 0; i < N; i++) {
        float t = (float)i / SR;
        float env = std::exp(-t * 17.0f);
        b[i] = ((nz() + nz()) * 0.5f) * env;        // a touch grainy
    }
    lowpass(b, 0.55f); normalize(b, 0.5f); return b;
}
std::vector<float> genBlockPlace() {
    int N = (int)(SR * 0.10f); std::vector<float> b(N);
    for (int i = 0; i < N; i++) {
        float t = (float)i / SR;
        float env  = std::exp(-t * 40.0f);
        float tone = std::sin(PI2 * 160.0f * t);
        b[i] = (tone * 0.7f + nz() * 0.3f) * env;
    }
    lowpass(b, 0.35f); normalize(b, 0.45f); return b;
}
std::vector<float> genSwing() {
    int N = (int)(SR * 0.22f); std::vector<float> b(N);
    for (int i = 0; i < N; i++) {
        float u = (float)i / N;
        b[i] = nz() * swell(u);                     // airy whoosh
    }
    lowpass(b, 0.12f); normalize(b, 0.4f); return b;
}
std::vector<float> genMeleeHit() {
    int N = (int)(SR * 0.13f); std::vector<float> b(N);
    for (int i = 0; i < N; i++) {
        float t = (float)i / SR;
        float env   = std::exp(-t * 30.0f);
        float thump = std::sin(PI2 * 90.0f * t) * std::exp(-t * 40.0f);
        b[i] = (nz() * 0.7f + thump) * env;
    }
    lowpass(b, 0.3f); normalize(b, 0.55f); return b;
}
std::vector<float> genBowShot() {
    int N = (int)(SR * 0.18f); std::vector<float> b(N); float ph = 0.0f;
    for (int i = 0; i < N; i++) {
        float t = (float)i / SR;
        float env = std::exp(-t * 22.0f);
        float f   = 120.0f + 300.0f * std::exp(-t * 7.0f);   // pitch drops
        ph += PI2 * f / SR;
        b[i] = (std::sin(ph) * 0.8f + nz() * 0.25f * std::exp(-t * 60.0f)) * env;
    }
    normalize(b, 0.45f); return b;
}
std::vector<float> genMagicCast() {
    int N = (int)(SR * 0.32f); std::vector<float> b(N); float ph = 0.0f;
    for (int i = 0; i < N; i++) {
        float t = (float)i / SR, u = (float)i / N;
        float env = swell(u);
        float f   = 300.0f + 600.0f * u + 40.0f * std::sin(PI2 * 7.0f * t);  // rise + vibrato
        ph += PI2 * f / SR;
        b[i] = (std::sin(ph) * 0.7f + std::sin(ph * 2.01f) * 0.2f) * env;
    }
    normalize(b, 0.4f); return b;
}
std::vector<float> genDoor(bool open) {
    int N = (int)(SR * 0.5f); std::vector<float> b(N); float ph = 0.0f;
    for (int i = 0; i < N; i++) {
        float t = (float)i / SR, u = (float)i / N;
        float env  = swell(u);
        float base = open ? (110.0f + 50.0f * u) : (150.0f - 50.0f * u);
        float f    = base + 18.0f * std::sin(PI2 * 9.0f * t);   // creak wobble
        ph += PI2 * f / SR;
        b[i] = (std::sin(ph) * 0.6f + nz() * 0.2f) * env;
    }
    lowpass(b, 0.25f); normalize(b, 0.4f); return b;
}
std::vector<float> genBird() {
    int N = (int)(SR * 0.2f); std::vector<float> b(N); float ph = 0.0f;
    for (int i = 0; i < N; i++) {
        float t = (float)i / SR;
        float seg = std::fmod(t, 0.09f);
        float blip = (seg < 0.05f) ? 1.0f : 0.0f;
        float env  = blip * swell(std::min(seg / 0.05f, 1.0f)) * std::exp(-t * 3.0f);
        float f    = 2600.0f + 700.0f * std::sin(PI2 * 30.0f * t);
        ph += PI2 * f / SR;
        b[i] = std::sin(ph) * env;
    }
    normalize(b, 0.3f); return b;
}
// A seamless ~3 s wind bed: muffled noise under a periodic amplitude LFO, with
// the tail crossfaded into the head so it loops without a click.
std::vector<float> genWindLoop() {
    int N = SR * 3, X = 2400; std::vector<float> b(N + X);
    for (float& s : b) s = nz();
    lowpass(b, 0.035f); lowpass(b, 0.035f);
    for (int i = 0; i < (int)b.size(); i++) {
        float u = (float)i / N;
        b[i] *= 0.6f + 0.4f * std::sin(PI2 * u) + 0.18f * std::sin(PI2 * 2.0f * u);
    }
    for (int i = 0; i < X; i++) { float a = (float)i / X; b[i] = b[i] * a + b[N + i] * (1.0f - a); }
    b.resize(N); normalize(b, 0.5f); return b;
}
// A seamless ~2.5 s hearth-fire bed: brown-noise rumble with scattered crackle
// pops, tail crossfaded into the head.
std::vector<float> genFireLoop() {
    int N = (int)(SR * 2.5f), X = 2400; std::vector<float> b(N + X, 0.0f);
    float brown = 0.0f;
    for (int i = 0; i < (int)b.size(); i++) { brown += nz() * 0.02f; brown *= 0.996f; b[i] = brown; }
    lowpass(b, 0.08f);
    for (int i = 0; i < (int)b.size(); i++)
        if (u01() < 0.0008f)
            for (int k = 0; k < 200 && i + k < (int)b.size(); k++)
                b[i + k] += nz() * std::exp(-k / 40.0f) * 0.6f;
    for (int i = 0; i < X; i++) { float a = (float)i / X; b[i] = b[i] * a + b[N + i] * (1.0f - a); }
    b.resize(N); normalize(b, 0.5f); return b;
}
}  // namespace

// --- Impl ------------------------------------------------------------------

struct AudioSystem::Impl {
    ma_engine engine{};
    bool      engineOk = false;

    std::vector<float> pcm[(int)SoundId::Count];   // one synthesised clip each

    struct Voice {
        ma_audio_buffer_ref ref{};
        ma_sound            sound{};
        bool                active = false;
    };
    Voice voices[MAX_VOICES];

    // Looping ambience beds.
    std::vector<float>  windPcm, firePcm;
    ma_audio_buffer_ref windRef{}, fireRef{};
    ma_sound            windSound{}, fireSound{};
    bool                windOk = false, fireOk = false;
    float               windVol = 0.0f, fireVol = 0.0f;

    float     masterVol   = 0.6f;
    glm::vec3 listenerPos{0.0f};
    float     listenerYaw = 0.0f;
    float     birdTimer   = 5.0f;

    void reap() {
        for (Voice& v : voices) {
            if (v.active && !ma_sound_is_playing(&v.sound)) {
                ma_sound_uninit(&v.sound);
                ma_audio_buffer_ref_uninit(&v.ref);
                v.active = false;
            }
        }
    }
    void playClip(int id, float vol, float pan, float pitch) {
        if (!engineOk || id < 0 || id >= (int)SoundId::Count || pcm[id].empty()) return;
        Voice* slot = nullptr;
        for (Voice& v : voices) if (!v.active) { slot = &v; break; }
        if (!slot) return;                          // pool full — drop the sound
        if (ma_audio_buffer_ref_init(ma_format_f32, 1, pcm[id].data(),
                                     (ma_uint64)pcm[id].size(), &slot->ref) != MA_SUCCESS)
            return;
        slot->ref.sampleRate = SR;
        if (ma_sound_init_from_data_source(&engine, &slot->ref,
                MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &slot->sound) != MA_SUCCESS) {
            ma_audio_buffer_ref_uninit(&slot->ref);
            return;
        }
        ma_sound_set_volume(&slot->sound, vol);
        ma_sound_set_pan(&slot->sound, pan);
        if (pitch != 1.0f) ma_sound_set_pitch(&slot->sound, pitch);
        ma_sound_start(&slot->sound);
        slot->active = true;
    }
};

bool AudioSystem::init() {
    impl = new Impl();
    ma_engine_config cfg = ma_engine_config_init();
    cfg.sampleRate = SR;
    if (ma_engine_init(&cfg, &impl->engine) != MA_SUCCESS) {
        delete impl; impl = nullptr; return false;   // no audio device — run silent
    }
    impl->engineOk = true;

    impl->pcm[(int)SoundId::Footstep]   = genFootstep();
    impl->pcm[(int)SoundId::BlockBreak] = genBlockBreak();
    impl->pcm[(int)SoundId::BlockPlace] = genBlockPlace();
    impl->pcm[(int)SoundId::Swing]      = genSwing();
    impl->pcm[(int)SoundId::MeleeHit]   = genMeleeHit();
    impl->pcm[(int)SoundId::BowShot]    = genBowShot();
    impl->pcm[(int)SoundId::MagicCast]  = genMagicCast();
    impl->pcm[(int)SoundId::DoorOpen]   = genDoor(true);
    impl->pcm[(int)SoundId::DoorClose]  = genDoor(false);
    impl->pcm[(int)SoundId::Bird]       = genBird();

    // Looping ambience beds, started at zero volume and modulated each frame.
    impl->windPcm = genWindLoop();
    impl->firePcm = genFireLoop();
    if (ma_audio_buffer_ref_init(ma_format_f32, 1, impl->windPcm.data(),
                                 (ma_uint64)impl->windPcm.size(), &impl->windRef) == MA_SUCCESS) {
        impl->windRef.sampleRate = SR;
        if (ma_sound_init_from_data_source(&impl->engine, &impl->windRef,
                MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &impl->windSound) == MA_SUCCESS) {
            ma_sound_set_looping(&impl->windSound, MA_TRUE);
            ma_sound_set_volume(&impl->windSound, 0.0f);
            ma_sound_start(&impl->windSound);
            impl->windOk = true;
        }
    }
    if (ma_audio_buffer_ref_init(ma_format_f32, 1, impl->firePcm.data(),
                                 (ma_uint64)impl->firePcm.size(), &impl->fireRef) == MA_SUCCESS) {
        impl->fireRef.sampleRate = SR;
        if (ma_sound_init_from_data_source(&impl->engine, &impl->fireRef,
                MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &impl->fireSound) == MA_SUCCESS) {
            ma_sound_set_looping(&impl->fireSound, MA_TRUE);
            ma_sound_set_volume(&impl->fireSound, 0.0f);
            ma_sound_start(&impl->fireSound);
            impl->fireOk = true;
        }
    }
    return true;
}

void AudioSystem::shutdown() {
    if (!impl) return;
    if (impl->engineOk) {
        for (Impl::Voice& v : impl->voices)
            if (v.active) { ma_sound_uninit(&v.sound); ma_audio_buffer_ref_uninit(&v.ref); v.active = false; }
        if (impl->windOk) { ma_sound_uninit(&impl->windSound); ma_audio_buffer_ref_uninit(&impl->windRef); }
        if (impl->fireOk) { ma_sound_uninit(&impl->fireSound); ma_audio_buffer_ref_uninit(&impl->fireRef); }
        ma_engine_uninit(&impl->engine);
    }
    delete impl; impl = nullptr;
}

bool AudioSystem::ready() const { return impl && impl->engineOk; }

void AudioSystem::setMasterVolume(float v) { if (impl) impl->masterVol = std::clamp(v, 0.0f, 1.0f); }

void AudioSystem::update(const glm::vec3& listenerPos, float listenerYaw, float dt,
                         float gameTime, float weather, float nearestFireDist) {
    if (!impl || !impl->engineOk) return;
    impl->listenerPos = listenerPos;
    impl->listenerYaw = listenerYaw;
    impl->reap();

    float ease = std::clamp(dt * 1.6f, 0.0f, 1.0f);
    // Wind: a quiet ever-present bed that swells in storms.
    if (impl->windOk) {
        float target = (0.05f + 0.30f * weather) * impl->masterVol;
        impl->windVol += (target - impl->windVol) * ease;
        ma_sound_set_volume(&impl->windSound, impl->windVol);
    }
    // Fire: rises as the listener nears a lit hearth or campfire.
    if (impl->fireOk) {
        float prox   = (nearestFireDist < 15.0f) ? (1.0f - nearestFireDist / 15.0f) : 0.0f;
        float target = prox * prox * 0.55f * impl->masterVol;
        impl->fireVol += (target - impl->fireVol) * ease;
        ma_sound_set_volume(&impl->fireSound, impl->fireVol);
    }
    // Birds: the odd chirp around the listener on fair days.
    float sunY = std::sin((gameTime - 0.25f) * PI2);
    impl->birdTimer -= dt;
    if (impl->birdTimer <= 0.0f) {
        impl->birdTimer = 5.0f + u01() * 9.0f;
        if (sunY > 0.25f && weather < 0.3f) {
            float a = u01() * PI2, r = 6.0f + u01() * 16.0f;
            glm::vec3 p(listenerPos.x + std::cos(a) * r,
                        listenerPos.y + 3.0f + u01() * 6.0f,
                        listenerPos.z + std::sin(a) * r);
            playAt(SoundId::Bird, p, 0.5f, 0.95f + u01() * 0.1f);
        }
    }
}

void AudioSystem::play2D(SoundId id, float gain, float pitch) {
    if (!impl || !impl->engineOk) return;
    impl->playClip((int)id, gain * impl->masterVol, 0.0f, pitch);
}

void AudioSystem::playAt(SoundId id, const glm::vec3& pos, float gain, float pitch) {
    if (!impl || !impl->engineOk) return;
    glm::vec3 d = pos - impl->listenerPos;
    float dist = std::sqrt(d.x * d.x + d.y * d.y * 0.5f + d.z * d.z);
    const float maxD = 36.0f;
    if (dist > maxD) return;
    float att = 1.0f - dist / maxD; att *= att;          // gentle quadratic rolloff
    float vol = gain * impl->masterVol * att;
    if (vol < 0.004f) return;
    float yaw = glm::radians(impl->listenerYaw);
    glm::vec3 right(std::cos(yaw), 0.0f, -std::sin(yaw));
    float hl = std::sqrt(d.x * d.x + d.z * d.z);
    float pan = (hl > 0.01f) ? std::clamp((d.x * right.x + d.z * right.z) / hl, -1.0f, 1.0f) * 0.85f
                             : 0.0f;
    impl->playClip((int)id, vol, pan, pitch);
}
