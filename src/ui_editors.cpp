// The two voxel editors — character creator and house designer — split out of
// ui.cpp. Each drives a 3D viewport via the Renderer plus an ImGui side panel.
// Shared viewport helpers live in ui_internal.h.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "ui.h"
#include "app_context.h"
#include "renderer.h"
#include "game_session.h"
#include "gameplay.h"
#include "town.h"
#include "npc.h"
#include "prop_placement.h"
#include "graphics_settings.h"
#include "gl_loader.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>
#include <iostream>
#include "ui_internal.h"

// ---------------------------------------------------------------------------
// Character editor UI
// ---------------------------------------------------------------------------

void renderCharacterEditorUI(AppContext& ctx, GLFWwindow* window, Renderer& renderer) {
    int fbW, fbH;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    glViewport(0, 0, fbW, fbH);

    glm::mat4 proj  = glm::perspective(glm::radians(45.0f), fbW / (float)fbH, 0.1f, 1000.0f);
    glm::mat4 model = glm::rotate(glm::mat4(1.0f), glm::radians(ctx.editorRotY), glm::vec3(0, 1, 0));
    model = glm::rotate(model, glm::radians(ctx.editorRotX), glm::vec3(1, 0, 0));
    model = glm::scale(model, glm::vec3(0.06f * (ctx.playerRig ? ctx.playerRig->heightScale : 1.0f)));
    glm::mat4 view  = glm::lookAt(glm::vec3(0, 1.5, 4), glm::vec3(0, 1.0, 0), glm::vec3(0, 1, 0));

    renderer.renderEditorCharacter(ctx, model, view, proj);

    // ImGui panel
    ImGui::SetNextWindowPos(vpPos(), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(300, vpSize().y), ImGuiCond_Always);
    ImGui::Begin("Character Editor", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (ImGui::Button("Back to Menu", ImVec2(-1, 0))) ctx.state = GameState::MainMenu;
    ImGui::Separator();

    if (ImGui::Combo("Character Type", &ctx.editorCharType, "Human Male\0Human Female\0")) {
        ctx.playerRig->setupDefaultHuman(ctx.editorCharType == 0);
        rebuildRigFromInventory(*ctx.playerRig, ctx.inventory);
    }

    // Role — sets the archetype, body size and base stats. Tanks are broad and
    // tall, DPS lean and short. Changing role resets the size sliders to the
    // role's defaults (they can then be fine-tuned below).
    int roleIdx = (int)ctx.playerRole;
    if (ImGui::Combo("Role", &roleIdx, "Tank\0DPS\0Healer\0")) {
        ctx.playerRole = (PlayerRole)roleIdx;
        ctx.playerRig->heightScale = roleHeightScale(ctx.playerRole);
        ctx.playerRig->weightScale = roleWeightScale(ctx.playerRole);
        if (ctx.playerRig->torso) {
            ctx.playerRig->torso->scale.x = ctx.playerRig->weightScale;
            ctx.playerRig->torso->scale.z = ctx.playerRig->weightScale;
        }
        ctx.setupRoleLoadout();   // reseed stats, resource and starting abilities
        rebuildRigFromInventory(*ctx.playerRig, ctx.inventory);
    }
    ImGui::TextDisabled("%s — wears up to %s armour",
                        roleName(ctx.playerRole),
                        ctx.playerRole == PlayerRole::Tank   ? "Plate" :
                        ctx.playerRole == PlayerRole::DPS    ? "Leather" : "Cloth");

    ImGui::Separator();
    if (ImGui::SliderFloat("Height", &ctx.playerRig->heightScale, 0.5f, 1.5f)) {}
    if (ImGui::SliderFloat("Weight", &ctx.playerRig->weightScale, 0.5f, 1.5f)) {
        if (ctx.playerRig->torso) {
            ctx.playerRig->torso->scale.x = ctx.playerRig->weightScale;
            ctx.playerRig->torso->scale.z = ctx.playerRig->weightScale;
        }
    }

    if (ImGui::CollapsingHeader("Face Features", ImGuiTreeNodeFlags_DefaultOpen)) {
        float sCol[3] = { ctx.playerRig->skinColor.r / 255.0f,
                          ctx.playerRig->skinColor.g / 255.0f,
                          ctx.playerRig->skinColor.b / 255.0f };
        if (ImGui::ColorEdit3("Skin Color", sCol)) {
            ctx.playerRig->skinColor = { (uint8_t)(sCol[0]*255), (uint8_t)(sCol[1]*255),
                                         (uint8_t)(sCol[2]*255), 255 };
            rebuildRigFromInventory(*ctx.playerRig, ctx.inventory);
        }
        if (ImGui::Combo("Hair Style", &ctx.playerRig->hairStyle,
                "Bald\0Crew Cut\0Messy Short\0Mohawk\0Spiky\0Side Swept\0"
                "Bob\0Long Straight\0Wavy Long\0Bun\0Pigtails\0Braided\0"))
            rebuildRigFromInventory(*ctx.playerRig, ctx.inventory);

        float hCol[3] = { ctx.playerRig->hairColor.r / 255.0f,
                          ctx.playerRig->hairColor.g / 255.0f,
                          ctx.playerRig->hairColor.b / 255.0f };
        if (ImGui::ColorEdit3("Hair Color", hCol)) {
            ctx.playerRig->hairColor = { (uint8_t)(hCol[0]*255), (uint8_t)(hCol[1]*255),
                                         (uint8_t)(hCol[2]*255), 255 };
            rebuildRigFromInventory(*ctx.playerRig, ctx.inventory);
        }
        if (ImGui::Combo("Eyebrow Style", &ctx.playerRig->eyebrowStyle,
                "Straight\0Arched\0Thick\0Thin\0Furrowed\0"))
            rebuildRigFromInventory(*ctx.playerRig, ctx.inventory);

        float eCol[3] = { ctx.playerRig->eyeColor.r / 255.0f,
                          ctx.playerRig->eyeColor.g / 255.0f,
                          ctx.playerRig->eyeColor.b / 255.0f };
        if (ImGui::ColorEdit3("Eye Color", eCol)) {
            ctx.playerRig->eyeColor = { (uint8_t)(eCol[0]*255), (uint8_t)(eCol[1]*255),
                                        (uint8_t)(eCol[2]*255), 255 };
            rebuildRigFromInventory(*ctx.playerRig, ctx.inventory);
        }
        if (ImGui::Combo("Eye Type", &ctx.playerRig->eyeType,
                "Classic\0Happy\0Wide\0Slanted\0Heart\0Wink\0Tired\0Star\0Tears\0Determined\0"))
            rebuildRigFromInventory(*ctx.playerRig, ctx.inventory);
        if (ImGui::Combo("Nose Style", &ctx.playerRig->noseStyle,
                "Button\0Wide\0Narrow\0Upturned\0Broad\0"))
            rebuildRigFromInventory(*ctx.playerRig, ctx.inventory);
        if (ImGui::Combo("Ear Type", &ctx.playerRig->earType,
                "None\0Human\0Elven\0Rounded\0Wide\0"))
            rebuildRigFromInventory(*ctx.playerRig, ctx.inventory);
        // Armour set picker removed — gear is now managed in-game from the
        // Character Loadout screen (press C while playing).
    }

    ImGui::Separator();
    ImGui::Text("Voxel Tools");
    if (ImGui::RadioButton("Paint", ctx.editorTool == EditorTool::Paint))
        ctx.editorTool = EditorTool::Paint;
    if (ImGui::RadioButton("Add",   ctx.editorTool == EditorTool::Add))
        ctx.editorTool = EditorTool::Add;
    if (ImGui::RadioButton("Erase", ctx.editorTool == EditorTool::Erase))
        ctx.editorTool = EditorTool::Erase;

    ImGui::Separator();
    ImGui::Text("Color");
    ImGui::ColorPicker4("##picker", (float*)&ctx.editorColor);

    ImGui::Separator();
    if (ImGui::Button("Save Model", ImVec2(-1, 0))) {}

    ImGui::End();

    // Mouse: rotation or voxel editing
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        if (!ImGui::GetIO().WantCaptureMouse) {
            float rx = (2.0f * (float)mx) / fbW - 1.0f;
            float ry = 1.0f - (2.0f * (float)my) / fbH;
            glm::vec4 clip(rx, ry, -1.0f, 1.0f);
            glm::vec4 eye = glm::inverse(proj) * clip;
            eye.z = -1.0f; eye.w = 0.0f;
            glm::vec3 rd = glm::normalize(glm::vec3(glm::inverse(view) * eye));
            glm::vec3 ro = glm::vec3(glm::inverse(view) * glm::vec4(0, 0, 0, 1));

            if (!ctx.wasEditorClick) {
                auto hit = ctx.playerRig->raycast(ro, rd, model);
                ctx.isEditorRotating = !hit.node;
                ctx.wasEditorClick   = true;
            }

            if (ctx.isEditorRotating) {
                ctx.editorRotY += (float)(mx - ctx.lastEditorX) * 0.5f;
                ctx.editorRotX += (float)(my - ctx.lastEditorY) * 0.5f;
                ctx.editorRotX  = std::clamp(ctx.editorRotX, -80.0f, 80.0f);
            } else {
                auto hit = ctx.playerRig->raycast(ro, rd, model);
                if (hit.node && hit.node->volume) {
                    Voxel cv = { (uint8_t)(ctx.editorColor.r * 255),
                                 (uint8_t)(ctx.editorColor.g * 255),
                                 (uint8_t)(ctx.editorColor.b * 255),
                                 (uint8_t)(ctx.editorColor.a * 255) };
                    if (ctx.editorTool == EditorTool::Paint) {
                        hit.node->volume->setVoxel(hit.voxel.x, hit.voxel.y, hit.voxel.z, cv);
                    } else if (ctx.editorTool == EditorTool::Add) {
                        glm::ivec3 ap = hit.voxel + hit.normal;
                        hit.node->volume->setVoxel(ap.x, ap.y, ap.z, cv);
                    } else {
                        hit.node->volume->setVoxel(hit.voxel.x, hit.voxel.y, hit.voxel.z, {0,0,0,0});
                    }
                    hit.node->volume->updateMesh();
                }
            }
        }
        ctx.lastEditorX = mx;
        ctx.lastEditorY = my;
    } else {
        ctx.wasEditorClick = false;
    }
}

// ---------------------------------------------------------------------------
// House editor UI
// ---------------------------------------------------------------------------

// Blocks the player can build a house from (parallel to the combo labels below).
// First the structural blocks, then every painted-palette colour.
static std::vector<BlockType> makeHouseBuildBlocks() {
    std::vector<BlockType> v = {
        BlockType::Wood, BlockType::Stone, BlockType::Glass,
        BlockType::Glowstone, BlockType::Lantern, BlockType::Leaves,
    };
    for (int i = 0; i < PAINT_COUNT; i++)
        v.push_back((BlockType)((int)BlockType::PaintFirst + i));
    return v;
}
static const std::vector<BlockType> kHouseBuildBlocks = makeHouseBuildBlocks();
static const char* kHouseBuildBlockLabels =
    "Wood\0Stone\0Glass\0Glowstone\0Lantern\0Leaves\0"
    "White\0Cream\0Light Gray\0Slate Gray\0Charcoal\0Black\0"
    "Terracotta\0Brick Red\0Crimson\0Rust Orange\0Amber\0Mustard\0"
    "Chestnut\0Sand\0Olive\0Sage\0Forest Green\0Mint\0"
    "Sky Blue\0Teal\0Navy\0Steel Blue\0Plum\0Dusty Rose\0";

void renderHouseEditorUI(AppContext& ctx, GLFWwindow* window, Renderer& renderer) {
    int fbW, fbH;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    glViewport(0, 0, fbW, fbH);

    HouseModel* house = ctx.houseModel;

    glm::mat4 proj  = glm::perspective(glm::radians(45.0f), fbW / (float)fbH, 0.1f, 1000.0f);
    glm::vec3 center(HOUSE_VX * 0.5f, 12.0f, HOUSE_VZ * 0.5f);
    glm::mat4 model = glm::rotate(glm::mat4(1.0f), glm::radians(ctx.editorRotY), glm::vec3(0, 1, 0));
    model = glm::rotate(model, glm::radians(ctx.editorRotX), glm::vec3(1, 0, 0));
    model = glm::scale(model, glm::vec3(0.28f));
    model = glm::translate(model, -center);
    glm::mat4 view  = glm::lookAt(glm::vec3(0, 0, ctx.camDist),
                                  glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));

    renderer.renderEditorHouse(ctx, model, view, proj);

    // ImGui panel
    ImGui::SetNextWindowPos(vpPos(), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(300, vpSize().y), ImGuiCond_Always);
    ImGui::Begin("House Editor", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (ImGui::Button("Back to Menu", ImVec2(-1, 0))) ctx.state = GameState::MainMenu;
    ImGui::Separator();

    if (house) {
        ImGui::Text("Template");
        if (ImGui::Combo("##template", &house->templateType,
                "Bungalow\0Two-Story\0Cottage\0Tower\0Cabin\0"
                "Longhouse\0Townhouse\0Manor\0Hall\0Keep\0"
                "Norse Longhouse\0Norse Mead Hall\0"
                "Tavern (Pub)\0Blacksmith\0Mage Tower\0"))
            house->rebuild();
        if (ImGui::Combo("Roof", &house->roofType,
                "Flat\0Gabled\0Hipped\0Pyramid\0Steep Gable (Norse)\0"))
            house->rebuild();
        if (ImGui::Combo("Material", &house->material,
                "Timber\0Cottage\0Stone\0Manor\0Cabin\0"
                "Sandstone\0Forest\0Coastal\0Autumn\0Plum\0"))
            house->rebuild();

        ImGui::Separator();
        if (ImGui::Button("Reset to Template", ImVec2(-1, 0)))
            house->rebuild();
    }

    ImGui::Separator();
    ImGui::Text("Edit Tools");
    if (ImGui::RadioButton("Paint", ctx.editorTool == EditorTool::Paint))
        ctx.editorTool = EditorTool::Paint;
    if (ImGui::RadioButton("Add",   ctx.editorTool == EditorTool::Add))
        ctx.editorTool = EditorTool::Add;
    if (ImGui::RadioButton("Erase", ctx.editorTool == EditorTool::Erase))
        ctx.editorTool = EditorTool::Erase;

    ImGui::Separator();
    ImGui::Text("Build Block");
    ImGui::Combo("##buildblock", &ctx.editorBlock, kHouseBuildBlockLabels);

    ImGui::Separator();
    ImGui::TextWrapped("Drag empty space to rotate, scroll to zoom. "
                       "Drag the house to paint/add/erase blocks.");
    ImGui::TextWrapped("The house is built from real world blocks, so it has full "
                       "collision once placed.");
    ImGui::Spacing();
    ImGui::TextWrapped("In game: press H to preview placement, H again to build it, "
                       "Esc to cancel.");

    ImGui::End();

    // Mouse: rotation or block editing
    if (house && house->volume &&
        glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        if (!ImGui::GetIO().WantCaptureMouse) {
            float rx = (2.0f * (float)mx) / fbW - 1.0f;
            float ry = 1.0f - (2.0f * (float)my) / fbH;
            glm::vec4 clip(rx, ry, -1.0f, 1.0f);
            glm::vec4 eye = glm::inverse(proj) * clip;
            eye.z = -1.0f; eye.w = 0.0f;
            glm::vec3 rd = glm::normalize(glm::vec3(glm::inverse(view) * eye));
            glm::vec3 ro = glm::vec3(glm::inverse(view) * glm::vec4(0, 0, 0, 1));

            glm::mat4 invM = glm::inverse(model);
            glm::vec3 lro  = glm::vec3(invM * glm::vec4(ro, 1.0f));
            glm::vec3 lrd  = glm::normalize(glm::vec3(invM * glm::vec4(rd, 0.0f)));
            glm::ivec3 hv, hn;

            if (!ctx.wasEditorClick) {
                ctx.isEditorRotating = !house->volume->raycast(lro, lrd, 500.0f, hv, hn);
                ctx.wasEditorClick   = true;
            }

            if (ctx.isEditorRotating) {
                ctx.editorRotY += (float)(mx - ctx.lastEditorX) * 0.5f;
                ctx.editorRotX += (float)(my - ctx.lastEditorY) * 0.5f;
                ctx.editorRotX  = std::clamp(ctx.editorRotX, -89.0f, 89.0f);
            } else if (house->volume->raycast(lro, lrd, 500.0f, hv, hn)) {
                const int blockCount = (int)kHouseBuildBlocks.size();
                BlockType placeB = kHouseBuildBlocks[std::clamp(ctx.editorBlock, 0, blockCount - 1)];
                if (ctx.editorTool == EditorTool::Paint) {
                    house->set(hv.x, hv.y, hv.z, placeB);
                } else if (ctx.editorTool == EditorTool::Add) {
                    glm::ivec3 ap = hv + hn;
                    house->set(ap.x, ap.y, ap.z, placeB);
                } else {
                    house->set(hv.x, hv.y, hv.z, BlockType::Air);
                }
                house->refreshMesh();
            }
        }
        ctx.lastEditorX = mx;
        ctx.lastEditorY = my;
    } else {
        ctx.wasEditorClick = false;
    }
}

