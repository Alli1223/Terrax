#pragma once
#include <glm/glm.hpp>
#include <cstdint>

// Lightweight client-side sound system backed by miniaudio (vendored in
// third_party/, header-only). Every sound is synthesised procedurally at init
// (no asset files), then played as positional one-shots — attenuated and panned
// by the listener — plus a couple of looping ambience beds (wind, hearth fire)
// whose volume is driven each frame. The dedicated server never creates one;
// all gameplay sound is purely client-side feedback.
enum class SoundId : uint8_t {
    Footstep = 0,
    BlockBreak,
    BlockPlace,
    Swing,        // melee swing whoosh
    MeleeHit,     // a connecting blow
    BowShot,      // bow release twang
    MagicCast,    // staff cast shimmer
    DoorOpen,
    DoorClose,
    Bird,         // occasional daytime chirp
    LevelUp,      // triumphant rising chime on level-up
    Quaff,        // gulp when drinking a potion / eating food
    EnemyDeath,   // a falling groan when a foe drops
    Count
};

class AudioSystem {
public:
    bool init();
    void shutdown();
    bool ready() const;

    // Per-frame upkeep: refresh the listener, reap finished one-shot voices, and
    // drive the wind / fire / bird ambience. `nearestFireDist` is the distance to
    // the closest lit hearth or campfire (pass a large value if none); `weather`
    // is the 0..1 storm intensity; `gameTime` is the 0..1 day fraction.
    void update(const glm::vec3& listenerPos, float listenerYaw, float dt,
                float gameTime, float weather, float nearestFireDist);

    // World-positioned one-shot (volume + pan derived from the listener) and a
    // flat non-positional one-shot (the local player's own actions).
    void playAt(SoundId id, const glm::vec3& pos, float gain = 1.0f, float pitch = 1.0f);
    void play2D(SoundId id, float gain = 1.0f, float pitch = 1.0f);

    void setMasterVolume(float v);

private:
    struct Impl;
    Impl* impl = nullptr;
};

// Client-only global, set in main() after init(); null on the dedicated server.
// Lets event sites (doors, combat, block edits) trigger sounds without threading
// a pointer through every call. Always guard with `if (g_audio) ...`.
extern AudioSystem* g_audio;
