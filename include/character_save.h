#pragma once
#include <cstdint>
#include <vector>
#include <string>

struct AppContext;

// ---------------------------------------------------------------------------
// Character roster persistence.
// ---------------------------------------------------------------------------
// A CharacterSave is one saved character with its FULL progression — name,
// appearance, role, level, skill points and the set of unlocked abilities. The
// roster (a vector of these) is written to a per-user file so characters persist
// across sessions and servers; the Join flow lets the player pick one or create
// a new one.
//
// IMPORTANT: this is serialised FIELD-BY-FIELD (see character_save.cpp), never
// as a raw struct blob — MSVC and g++ pad/align differently and a raw write
// would be unreadable on the other build. Keep the on-disk format in sync with
// the (de)serialisers and bump SAVE_VERSION on any field change.

struct CharacterSave {
    char     name[32]     = {};            // MAX_PLAYER_NAME (31) + 1
    uint8_t  role         = 1;             // PlayerRole: Tank=0, DPS=1, Healer=2
    int32_t  level        = 1;
    int32_t  skillPoints  = 0;
    uint64_t unlockedMask = 0;             // bit (1ull << (int)AbilityId) per unlocked ability
    uint16_t hotbar[8]    = {};            // (uint16_t)AbilityId per slot, 0 = None
    int32_t  charType     = 0;             // 0 male, 1 female (setupDefaultHuman)
    int32_t  hairStyle = 0, eyeType = 0, noseStyle = 0, eyebrowStyle = 0, earType = 0;
    uint8_t  hairColor[3] = {60, 40, 20};
    uint8_t  eyeColor[3]  = {0, 0, 0};
    uint8_t  skinColor[3] = {210, 160, 130};
    float    heightScale  = 1.0f, weightScale = 1.0f;
};

// Load every saved character (empty vector if the file is missing/invalid).
std::vector<CharacterSave> loadRoster();
// Overwrite the roster file with `roster`. Returns false on I/O failure.
bool                       saveRoster(const std::vector<CharacterSave>& roster);

// Load a saved character into the live AppContext: name, role, level, skill
// points, the rig's appearance, the unlocked-ability book and the hotbar.
void          applyCharacterToContext(AppContext& ctx, const CharacterSave& cs);
// Snapshot the live AppContext's current character into a CharacterSave.
CharacterSave captureCharacterFromContext(const AppContext& ctx);
