#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "input.h"
#include "app_context.h"
#include "game_session.h"
#include "gameplay.h"
#include "network.h"
#include "audio.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

static void framebuffer_size_callback(GLFWwindow*, int w, int h) {
    glViewport(0, 0, w, h);
}

static void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    AppContext& ctx = *static_cast<AppContext*>(glfwGetWindowUserPointer(window));
    if (ImGui::GetIO().WantCaptureMouse) return;
    if (ctx.state == GameState::Paused || ctx.chatOpen || ctx.showMap) return;
    if (ctx.showInventory || ctx.showCharacterLoadout || ctx.showTrainer || ctx.showQuestGiver || ctx.showVendor || ctx.showQuestLog) return;
    if (ctx.state != GameState::Playing && ctx.state != GameState::CharacterEditor) return;
    if (ctx.firstMouse) { ctx.lastMouseX = xpos; ctx.lastMouseY = ypos; ctx.firstMouse = false; }
    float xoff = (float)(xpos - ctx.lastMouseX);
    float yoff = (float)(ctx.lastMouseY - ypos);
    ctx.lastMouseX = xpos;
    ctx.lastMouseY = ypos;
    ctx.camera.processMouseMovement(xoff, yoff);
}

static void mouse_button_callback(GLFWwindow* window, int button, int action, int) {
    AppContext& ctx = *static_cast<AppContext*>(glfwGetWindowUserPointer(window));
    if (ImGui::GetIO().WantCaptureMouse) return;
    if (ctx.state != GameState::Playing || ctx.paused || ctx.chatOpen) return;
    if (ctx.showInventory || ctx.showCharacterLoadout || ctx.showTrainer || ctx.showQuestGiver || ctx.showVendor || ctx.showQuestLog) return;
    if (!ctx.client || ctx.showMap) return;

    // Right mouse with a shield equipped raises the shield instead of
    // placing a block. Right release lowers it. Block-place still works
    // when no shield is equipped.
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        // A main-hand weapon with a secondary attack (the healing staff's AOE)
        // owns the right button — gameplay polls it per frame, so swallow the
        // click here instead of placing a block.
        Item* mh = ctx.inventory.equipped(EquipSlot::MainHand);
        if (mh && mh->getKind() == ItemKind::Weapon
            && static_cast<WeaponItem*>(mh)->hasSecondaryAttack())
            return;

        Item* off = ctx.inventory.equipped(EquipSlot::OffHand);
        bool hasShield = off && off->getKind() == ItemKind::Weapon
            && static_cast<WeaponItem*>(off)->getType() == WeaponType::Shield;
        if (hasShield) {
            ctx.shieldRaised = (action == GLFW_PRESS);
            return;
        }
    }

    if (action != GLFW_PRESS) return;

    glm::ivec3 hitBlock, hitNormal;
    if (ctx.world.raycast(ctx.camera.position + glm::vec3(0.0f, 1.6f, 0.0f),
                          ctx.camera.front, REACH, hitBlock, hitNormal)) {
        BlockUpdatePacket p;
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            p = { hitBlock.x, hitBlock.y, hitBlock.z, (uint8_t)BlockType::Air };
            ctx.client->send(PacketType::BlockUpdate, &p, sizeof(p));
            if (g_audio) g_audio->play2D(SoundId::BlockBreak, 0.5f);
        } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            glm::ivec3 place = hitBlock + hitNormal;
            if (ctx.world.getBlock(place.x, place.y, place.z) == BlockType::Air) {
                p = { place.x, place.y, place.z, (uint8_t)BlockType::Stone };
                ctx.client->send(PacketType::BlockUpdate, &p, sizeof(p));
                if (g_audio) g_audio->play2D(SoundId::BlockPlace, 0.5f);
            }
        }
    }
}

static void scroll_callback(GLFWwindow* window, double, double yoffset) {
    AppContext& ctx = *static_cast<AppContext*>(glfwGetWindowUserPointer(window));
    if (ImGui::GetIO().WantCaptureMouse) return;
    ctx.camDist -= (float)yoffset;
    ctx.camDist = std::clamp(ctx.camDist, 2.0f, 20.0f);
}

static void key_callback(GLFWwindow* window, int key, int, int action, int) {
    AppContext& ctx = *static_cast<AppContext*>(glfwGetWindowUserPointer(window));

    if (ctx.state == GameState::Playing || ctx.state == GameState::Paused) {
        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
            if (ctx.chatOpen) { ctx.chatOpen = false; return; }
            if (ctx.showMap) {
                ctx.showMap = false;
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                ctx.firstMouse = true;
                return;
            }
            if (ctx.showInventory || ctx.showCharacterLoadout || ctx.showTrainer || ctx.showQuestGiver || ctx.showVendor || ctx.showQuestLog) {
                ctx.showInventory = false;
                ctx.showCharacterLoadout = false;
                ctx.showTrainer = false;
                ctx.showQuestGiver = false;
                ctx.showVendor = false;
                ctx.showQuestLog = false;
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                ctx.firstMouse = true;
                return;
            }
            if (ctx.housePreviewActive) { ctx.housePreviewActive = false; return; }
            ctx.paused = !ctx.paused;
            ctx.state  = ctx.paused ? GameState::Paused : GameState::Playing;
            glfwSetInputMode(window, GLFW_CURSOR,
                             ctx.paused ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
            if (!ctx.paused) ctx.firstMouse = true;
            return;
        }
        if (key == GLFW_KEY_T && action == GLFW_PRESS && !ctx.paused) {
            ctx.chatOpen = !ctx.chatOpen;
            if (ctx.chatOpen) {
                ctx.chatInput[0] = '\0';
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            } else {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                ctx.firstMouse = true;
            }
            return;
        }
        if (key == GLFW_KEY_TAB) {
            ctx.showPlayerList = (action == GLFW_PRESS);
            return;
        }
    }

    if (ImGui::GetIO().WantCaptureKeyboard && ctx.state != GameState::JoinMenu) return;

    // Inventory / character loadout toggles must fire even while an overlay
    // is open (so the same key closes it). Movement keys are cleared on
    // open so we don't get "stuck" forward motion while the menu is up.
    if (ctx.state == GameState::Playing && !ctx.paused && !ctx.chatOpen) {
        auto clearMovement = [&]() {
            ctx.keyFwd = ctx.keyBack = ctx.keyLeft = ctx.keyRight = 0;
            ctx.keyJump = ctx.keySprint = 0;
        };
        // I and C both pull up the unified equipment screen (inventory grid
        // + character loadout, side by side) so the player can drag-drop
        // between them. Pressing either while it's open closes the whole
        // screen.
        if ((key == GLFW_KEY_I || key == GLFW_KEY_C) && action == GLFW_PRESS) {
            bool open = !(ctx.showInventory || ctx.showCharacterLoadout);
            ctx.showInventory        = open;
            ctx.showCharacterLoadout = open;
            if (open) { ctx.showSkillTree = false; ctx.showQuestLog = false; }   // one overlay at a time
            glfwSetInputMode(window, GLFW_CURSOR, open ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
            if (open) clearMovement(); else ctx.firstMouse = true;
            return;
        }
        // K opens the skill tree (and closes it again). Mutually exclusive with
        // the equipment screen so the cursor state stays consistent.
        if (key == GLFW_KEY_K && action == GLFW_PRESS) {
            ctx.showSkillTree = !ctx.showSkillTree;
            if (ctx.showSkillTree) { ctx.showInventory = false; ctx.showCharacterLoadout = false; ctx.showQuestLog = false; }
            glfwSetInputMode(window, GLFW_CURSOR, ctx.showSkillTree ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
            if (ctx.showSkillTree) clearMovement(); else ctx.firstMouse = true;
            return;
        }
        // J opens the quest journal (and closes it again).
        if (key == GLFW_KEY_J && action == GLFW_PRESS) {
            ctx.showQuestLog = !ctx.showQuestLog;
            if (ctx.showQuestLog) { ctx.showInventory = false; ctx.showCharacterLoadout = false; ctx.showSkillTree = false; }
            glfwSetInputMode(window, GLFW_CURSOR, ctx.showQuestLog ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
            if (ctx.showQuestLog) clearMovement(); else ctx.firstMouse = true;
            return;
        }
    }

    if (ctx.state == GameState::Playing && !ctx.paused && !ctx.chatOpen
        && !ctx.showInventory && !ctx.showCharacterLoadout) {
        if (key == GLFW_KEY_M && action == GLFW_PRESS) {
            ctx.showMap = !ctx.showMap;
            if (ctx.showMap) {
                ctx.mapBuiltCX     = ctx.camera.position.x;
                ctx.mapBuiltCZ     = ctx.camera.position.z;
                ctx.mapNeedsRebuild = true;
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            } else {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                ctx.firstMouse = true;
            }
        }
        if (key == GLFW_KEY_N && action == GLFW_PRESS) ctx.noclip = !ctx.noclip;
        if (key == GLFW_KEY_F3 && action == GLFW_PRESS) ctx.showDebugOverlay = !ctx.showDebugOverlay;
        if (key == GLFW_KEY_F2 && action == GLFW_PRESS) { ctx.requestScreenshot = true; ctx.screenshotTag.clear(); }
        if (key == GLFW_KEY_H && action == GLFW_PRESS && ctx.houseModel && ctx.client) {
            if (!ctx.housePreviewActive) {
                ctx.housePreviewActive = true;     // first press: show placement ghost
            } else {
                sendHousePlacement(ctx);           // second press: confirm + bake
                ctx.housePreviewActive = false;
            }
        }
        if (key == GLFW_KEY_W)     { if(action==GLFW_PRESS) ctx.keyFwd=1;   else if(action==GLFW_RELEASE) ctx.keyFwd=0; }
        if (key == GLFW_KEY_S)     { if(action==GLFW_PRESS) ctx.keyBack=1;  else if(action==GLFW_RELEASE) ctx.keyBack=0; }
        if (key == GLFW_KEY_A)     { if(action==GLFW_PRESS) ctx.keyLeft=1;  else if(action==GLFW_RELEASE) ctx.keyLeft=0; }
        if (key == GLFW_KEY_D)     { if(action==GLFW_PRESS) ctx.keyRight=1; else if(action==GLFW_RELEASE) ctx.keyRight=0; }
        if (key == GLFW_KEY_SPACE)      { if(action==GLFW_PRESS) ctx.keyJump=1;   else if(action==GLFW_RELEASE) ctx.keyJump=0; }
        if (key == GLFW_KEY_LEFT_SHIFT) { if(action==GLFW_PRESS) ctx.keySprint=1; else if(action==GLFW_RELEASE) ctx.keySprint=0; }
        if (key == GLFW_KEY_F && action == GLFW_PRESS) ctx.lanternHeld = !ctx.lanternHeld;
        if (key == GLFW_KEY_E && action == GLFW_PRESS) ctx.interactPressed = true;
        if (key == GLFW_KEY_T && action == GLFW_PRESS) ctx.cycleTargetPressed = true;
        // V — one-shot wave animation. Easy template for any future
        // emote: pick a ClipKind, call playClip on the rig with a
        // duration. The animation system handles the rest.
        // V — dodge roll (consumed in gameplay.cpp: dashes + grants i-frames).
        if (key == GLFW_KEY_V && action == GLFW_PRESS) ctx.rollPressed = true;
        // Number keys 1..8 fire the matching hotbar ability slot. The activation
        // (cooldown + resource checks) happens in gameplay.cpp via the flag.
        if (key >= GLFW_KEY_1 && key <= GLFW_KEY_8 && action == GLFW_PRESS)
            ctx.pendingHotbarSlot = key - GLFW_KEY_1;
    }
}

void setupInputCallbacks(GLFWwindow* window, AppContext& ctx) {
    glfwSetWindowUserPointer(window, &ctx);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetKeyCallback(window, key_callback);
}
