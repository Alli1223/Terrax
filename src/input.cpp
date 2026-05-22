#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "input.h"
#include "app_context.h"
#include "game_session.h"
#include "gameplay.h"
#include "network.h"
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
    if (action != GLFW_PRESS || !ctx.client || ctx.showMap) return;
    glm::ivec3 hitBlock, hitNormal;
    if (ctx.world.raycast(ctx.camera.position + glm::vec3(0.0f, 1.6f, 0.0f),
                          ctx.camera.front, REACH, hitBlock, hitNormal)) {
        BlockUpdatePacket p;
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            p = { hitBlock.x, hitBlock.y, hitBlock.z, (uint8_t)BlockType::Air };
            ctx.client->send(PacketType::BlockUpdate, &p, sizeof(p));
        } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            glm::ivec3 place = hitBlock + hitNormal;
            if (ctx.world.getBlock(place.x, place.y, place.z) == BlockType::Air) {
                p = { place.x, place.y, place.z, (uint8_t)BlockType::Stone };
                ctx.client->send(PacketType::BlockUpdate, &p, sizeof(p));
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

    if (ctx.state == GameState::Playing && !ctx.paused && !ctx.chatOpen) {
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
