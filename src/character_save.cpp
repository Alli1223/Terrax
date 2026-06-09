#include "character_save.h"
#include "app_context.h"
#include "voxel_model.h"
#include "inventory.h"   // rebuildRigFromInventory
#include "ability.h"     // AbilityId
#include "role.h"        // PlayerRole
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <direct.h>      // _mkdir
#else
#include <sys/stat.h>    // mkdir
#endif

// The unlock bitmask is a uint64_t, so every AbilityId ordinal must fit in 64
// bits. Execute is currently the highest. If a future content drop pushes this
// past 63 the save format must widen (and SAVE_VERSION must bump).
static_assert((int)AbilityId::Execute < 64, "CharacterSave::unlockedMask is uint64_t — too many AbilityIds");

namespace {

constexpr uint32_t SAVE_VERSION = 1;

void ensureDir(const std::string& d) {
#ifdef _WIN32
    _mkdir(d.c_str());
#else
    mkdir(d.c_str(), 0755);
#endif
}

// Per-user roster directory + file. Windows: %APPDATA%\Terrax. Linux/macOS:
// $XDG_DATA_HOME or ~/.local/share, then /Terrax. Falls back to the working dir
// if no env var is set. The directory is created if missing.
std::string rosterPath() {
#ifdef _WIN32
    const char* base = std::getenv("APPDATA");
    std::string dir  = base ? std::string(base) + "\\Terrax" : std::string(".");
    ensureDir(dir);
    return dir + "\\characters.sav";
#else
    const char* xdg  = std::getenv("XDG_DATA_HOME");
    const char* home = std::getenv("HOME");
    std::string dir  = xdg  ? std::string(xdg) + "/Terrax"
                     : home ? std::string(home) + "/.local/share/Terrax"
                            : std::string(".");
    ensureDir(dir);
    return dir + "/characters.sav";
#endif
}

void wr(FILE* f, const void* p, size_t n) { fwrite(p, 1, n, f); }
bool rd(FILE* f, void* p, size_t n)       { return fread(p, 1, n, f) == n; }

}  // namespace

std::vector<CharacterSave> loadRoster() {
    std::vector<CharacterSave> out;
    FILE* f = std::fopen(rosterPath().c_str(), "rb");
    if (!f) return out;

    char     magic[4] = {};
    uint32_t ver = 0, count = 0;
    if (!rd(f, magic, 4) || std::memcmp(magic, "TRXC", 4) != 0 ||
        !rd(f, &ver, 4)  || ver != SAVE_VERSION ||
        !rd(f, &count, 4)) { std::fclose(f); return out; }

    for (uint32_t i = 0; i < count; i++) {
        CharacterSave c;
        bool ok = rd(f, c.name, sizeof(c.name)) && rd(f, &c.role, 1) &&
                  rd(f, &c.level, 4) && rd(f, &c.skillPoints, 4) &&
                  rd(f, &c.unlockedMask, 8);
        for (int j = 0; j < 8 && ok; j++) ok = rd(f, &c.hotbar[j], 2);
        ok = ok && rd(f, &c.charType, 4) &&
             rd(f, &c.hairStyle, 4) && rd(f, &c.eyeType, 4) && rd(f, &c.noseStyle, 4) &&
             rd(f, &c.eyebrowStyle, 4) && rd(f, &c.earType, 4) &&
             rd(f, c.hairColor, 3) && rd(f, c.eyeColor, 3) && rd(f, c.skinColor, 3) &&
             rd(f, &c.heightScale, 4) && rd(f, &c.weightScale, 4);
        if (!ok) break;                       // truncated file — keep what parsed
        c.name[sizeof(c.name) - 1] = '\0';
        out.push_back(c);
    }
    std::fclose(f);
    return out;
}

bool saveRoster(const std::vector<CharacterSave>& roster) {
    FILE* f = std::fopen(rosterPath().c_str(), "wb");
    if (!f) return false;

    const char magic[4] = {'T', 'R', 'X', 'C'};
    uint32_t ver   = SAVE_VERSION;
    uint32_t count = (uint32_t)roster.size();
    wr(f, magic, 4); wr(f, &ver, 4); wr(f, &count, 4);

    for (const CharacterSave& c : roster) {
        wr(f, c.name, sizeof(c.name));
        wr(f, &c.role, 1);
        wr(f, &c.level, 4); wr(f, &c.skillPoints, 4);
        wr(f, &c.unlockedMask, 8);
        for (int j = 0; j < 8; j++) wr(f, &c.hotbar[j], 2);
        wr(f, &c.charType, 4);
        wr(f, &c.hairStyle, 4); wr(f, &c.eyeType, 4); wr(f, &c.noseStyle, 4);
        wr(f, &c.eyebrowStyle, 4); wr(f, &c.earType, 4);
        wr(f, c.hairColor, 3); wr(f, c.eyeColor, 3); wr(f, c.skinColor, 3);
        wr(f, &c.heightScale, 4); wr(f, &c.weightScale, 4);
    }
    std::fclose(f);
    return true;
}

void applyCharacterToContext(AppContext& ctx, const CharacterSave& cs) {
    // Identity + progression.
    std::snprintf(ctx.playerName, sizeof(ctx.playerName), "%s", cs.name);
    ctx.playerRole     = (PlayerRole)cs.role;
    ctx.playerLevel    = (cs.level > 0) ? cs.level : 1;
    ctx.skillPoints    = cs.skillPoints;
    ctx.editorCharType = cs.charType;

    // Appearance — base human first, then override every feature, mirroring the
    // character editor's apply path (ui_editors.cpp).
    if (BipedalRig* r = ctx.playerRig) {
        r->setupDefaultHuman(cs.charType == 0);
        r->hairStyle    = cs.hairStyle;    r->eyeType  = cs.eyeType;
        r->noseStyle    = cs.noseStyle;    r->earType  = cs.earType;
        r->eyebrowStyle = cs.eyebrowStyle;
        r->hairColor = { cs.hairColor[0], cs.hairColor[1], cs.hairColor[2], 255 };
        r->eyeColor  = { cs.eyeColor[0],  cs.eyeColor[1],  cs.eyeColor[2],  255 };
        r->skinColor = { cs.skinColor[0], cs.skinColor[1], cs.skinColor[2], 255 };
        r->heightScale = cs.heightScale;   r->weightScale = cs.weightScale;
        if (r->torso) { r->torso->scale.x = cs.weightScale; r->torso->scale.z = cs.weightScale; }
    }

    // Rebuild the ability book + unlocked set from the bitmask. Deliberately NOT
    // setupRoleLoadout() — that clears the unlocked/hotbar progression we restore.
    ctx.unlockedAbilities.clear();
    ctx.abilityBook.clear();
    ctx.activeBuffs.clear();
    for (int i = 1; i < 64; i++) {
        if (cs.unlockedMask & (1ull << i)) {
            AbilityId id = (AbilityId)i;
            ctx.unlockedAbilities.insert(id);
            ctx.grantAbility(id);
        }
    }
    for (int i = 0; i < AppContext::HOTBAR_SLOTS && i < 8; i++) {
        ctx.hotbar[i]         = (AbilityId)cs.hotbar[i];
        ctx.hotbarCooldown[i] = 0.0f;
    }
    ctx.recomputeRoleStats();          // stats + resource type/cap from role + level
    ctx.resource = ctx.resourceMax;

    if (ctx.playerRig) rebuildRigFromInventory(*ctx.playerRig, ctx.inventory);
}

CharacterSave captureCharacterFromContext(const AppContext& ctx) {
    CharacterSave cs;
    std::snprintf(cs.name, sizeof(cs.name), "%s", ctx.playerName);
    cs.role        = (uint8_t)ctx.playerRole;
    cs.level       = ctx.playerLevel;
    cs.skillPoints = ctx.skillPoints;

    cs.unlockedMask = 0;
    for (AbilityId id : ctx.unlockedAbilities) {
        int i = (int)id;
        if (i > 0 && i < 64) cs.unlockedMask |= (1ull << i);
    }
    for (int i = 0; i < 8 && i < AppContext::HOTBAR_SLOTS; i++)
        cs.hotbar[i] = (uint16_t)ctx.hotbar[i];

    cs.charType = ctx.editorCharType;
    if (const BipedalRig* r = ctx.playerRig) {
        cs.hairStyle = r->hairStyle; cs.eyeType = r->eyeType; cs.noseStyle = r->noseStyle;
        cs.eyebrowStyle = r->eyebrowStyle; cs.earType = r->earType;
        cs.hairColor[0] = r->hairColor.r; cs.hairColor[1] = r->hairColor.g; cs.hairColor[2] = r->hairColor.b;
        cs.eyeColor[0]  = r->eyeColor.r;  cs.eyeColor[1]  = r->eyeColor.g;  cs.eyeColor[2]  = r->eyeColor.b;
        cs.skinColor[0] = r->skinColor.r; cs.skinColor[1] = r->skinColor.g; cs.skinColor[2] = r->skinColor.b;
        cs.heightScale = r->heightScale; cs.weightScale = r->weightScale;
    }
    return cs;
}
