#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "ui.h"
#include "app_context.h"
#include "gameplay.h"
#include "clothing_painter.h"
#include "weapon_builder.h"
#include "item_generator.h"
#include "imgui.h"
#include <cstring>
#include <random>
#include <string>

// ---------------------------------------------------------------------------
// Inventory + character loadout UI
// ---------------------------------------------------------------------------
// The inventory is a 6x4 grid of cells with drag-drop reordering. The
// character loadout shows equipment slots arranged in a humanoid silhouette
// — every slot is a drop target that accepts items of the matching kind.
//
// Why this is in its own file: keeping the grid widget, icon rendering and
// drag-drop logic out of ui.cpp makes it easier to swap out for a custom
// UI later. Today it leans on ImGui's drag-drop and DrawList primitives.

namespace {

// --- Drag-drop protocol -------------------------------------------------
// One payload type covers grid-to-grid moves and inventory-to-equip moves.
// The payload is the source grid index — the receiver looks up the actual
// item via `ctx.inventory.at(idx)` so we don't carry pointers across frames.
constexpr const char* INV_DRAG_TYPE = "INV_ITEM";

ImVec4 voxelToImColor(Voxel v) {
    return ImVec4(v.r / 255.0f, v.g / 255.0f, v.b / 255.0f, v.a / 255.0f);
}

ImU32 voxelToU32(Voxel v) {
    return IM_COL32(v.r, v.g, v.b, v.a);
}

// Pull the two colour fields off an item (whichever subclass it is).
void itemColors(const Item* item, Voxel& pri, Voxel& acc) {
    if (item && item->getKind() == ItemKind::Clothing) {
        const ClothingItem* c = static_cast<const ClothingItem*>(item);
        pri = c->primaryColor;
        acc = c->accentColor;
    } else if (item && item->getKind() == ItemKind::Weapon) {
        const WeaponItem* w = static_cast<const WeaponItem*>(item);
        pri = w->primaryColor;
        acc = w->accentColor;
    } else {
        pri = {180, 180, 180, 255};
        acc = {120, 120, 120, 255};
    }
}

// Subtitle line for tooltips — "Cloth Helmet" / "Sword" etc.
std::string itemSubtitle(const Item* item) {
    if (!item) return {};
    if (item->getKind() == ItemKind::Clothing) {
        const ClothingItem* c = static_cast<const ClothingItem*>(item);
        return std::string(tierName(c->getTier())) + " " + slotName(c->getSlot());
    }
    if (item->getKind() == ItemKind::Weapon) {
        const WeaponItem* w = static_cast<const WeaponItem*>(item);
        return weaponTypeName(w->getType());
    }
    return {};
}

// --- Stylised item icon ------------------------------------------------
// Draws a small voxel-flavoured silhouette per item kind/slot/weapon type.
// Centred at `c`, sized to fit a `size` x `size` box. Uses the item's two
// colour fields so legendary palettes shine through even at icon scale.
void drawItemIcon(ImDrawList* dl, ImVec2 c, float size, const Item* item) {
    if (!item) return;
    Voxel priV, accV;
    itemColors(item, priV, accV);
    ImU32 pri = voxelToU32(priV);
    ImU32 acc = voxelToU32(accV);
    float h = size * 0.5f;

    auto rect = [&](float x0, float y0, float x1, float y1, ImU32 col) {
        dl->AddRectFilled(ImVec2(c.x + x0 * h, c.y + y0 * h),
                          ImVec2(c.x + x1 * h, c.y + y1 * h), col);
    };

    if (item->getKind() == ItemKind::Clothing) {
        const ClothingItem* ci = static_cast<const ClothingItem*>(item);
        switch (ci->getSlot()) {
            case EquipSlot::Helmet:
                // Dome + brim.
                dl->AddCircleFilled(ImVec2(c.x, c.y - h * 0.1f), h * 0.55f, pri, 18);
                rect(-0.75f, -0.05f, 0.75f, 0.15f, acc);
                break;
            case EquipSlot::Shoulders:
                // Two pads with a connecting strap.
                rect(-0.85f, -0.35f, -0.20f, 0.30f, pri);
                rect( 0.20f, -0.35f,  0.85f, 0.30f, pri);
                rect(-0.85f, -0.35f,  0.85f, -0.20f, acc);
                break;
            case EquipSlot::Chest:
                // Body + vertical placket.
                rect(-0.60f, -0.75f, 0.60f, 0.55f, pri);
                rect(-0.08f, -0.65f, 0.08f, 0.45f, acc);
                // Shoulder caps.
                rect(-0.78f, -0.75f, -0.55f, -0.40f, pri);
                rect( 0.55f, -0.75f,  0.78f, -0.40f, pri);
                break;
            case EquipSlot::Legs:
                // Two pant legs + waistband.
                rect(-0.55f, -0.50f, 0.55f, -0.35f, acc);
                rect(-0.50f, -0.35f, -0.08f, 0.75f, pri);
                rect( 0.08f, -0.35f,  0.50f, 0.75f, pri);
                break;
            case EquipSlot::Feet:
                // Two stubby boots with a sole stripe.
                rect(-0.75f, 0.00f, -0.10f, 0.55f, pri);
                rect( 0.10f, 0.00f,  0.75f, 0.55f, pri);
                rect(-0.80f, 0.50f,  0.80f, 0.65f, acc);
                break;
            default: break;
        }
    } else if (item->getKind() == ItemKind::Weapon) {
        const WeaponItem* wi = static_cast<const WeaponItem*>(item);
        switch (wi->getType()) {
            case WeaponType::Sword:
                // Vertical blade + cross guard + hilt.
                rect(-0.06f, -0.85f, 0.06f, 0.30f, pri);
                rect(-0.40f, 0.25f,  0.40f, 0.40f, acc);
                rect(-0.08f, 0.40f,  0.08f, 0.75f, acc);
                break;
            case WeaponType::Shield: {
                // Heater shape: five-sided convex poly + small boss.
                ImVec2 pts[5] = {
                    ImVec2(c.x,             c.y - h * 0.80f),
                    ImVec2(c.x + h * 0.70f, c.y - h * 0.45f),
                    ImVec2(c.x + h * 0.50f, c.y + h * 0.70f),
                    ImVec2(c.x - h * 0.50f, c.y + h * 0.70f),
                    ImVec2(c.x - h * 0.70f, c.y - h * 0.45f),
                };
                dl->AddConvexPolyFilled(pts, 5, pri);
                dl->AddCircleFilled(c, h * 0.18f, acc, 14);
                break;
            }
            case WeaponType::Bow: {
                // Curved limb (cubic bezier) + straight string.
                ImVec2 top   (c.x + h * 0.50f, c.y - h * 0.80f);
                ImVec2 bot   (c.x + h * 0.50f, c.y + h * 0.80f);
                ImVec2 ctrl1 (c.x - h * 0.95f, c.y - h * 0.30f);
                ImVec2 ctrl2 (c.x - h * 0.95f, c.y + h * 0.30f);
                dl->AddBezierCubic(top, ctrl1, ctrl2, bot, pri, 3.0f);
                dl->AddLine(top, bot, acc, 1.2f);
                break;
            }
            case WeaponType::Staff:
                // Shaft + orb.
                rect(-0.06f, -0.35f, 0.06f, 0.85f, pri);
                dl->AddCircleFilled(ImVec2(c.x, c.y - h * 0.60f), h * 0.22f, acc, 16);
                break;
            case WeaponType::Axe: {
                // Handle (accent) + head (primary).
                rect(-0.08f, -0.85f, 0.08f, 0.80f, acc);
                ImVec2 pts[4] = {
                    ImVec2(c.x + h * 0.08f, c.y - h * 0.75f),
                    ImVec2(c.x + h * 0.85f, c.y - h * 0.55f),
                    ImVec2(c.x + h * 0.55f, c.y - h * 0.05f),
                    ImVec2(c.x + h * 0.08f, c.y - h * 0.20f),
                };
                dl->AddConvexPolyFilled(pts, 4, pri);
                break;
            }
            default: break;
        }
    }

    // Legendary items get a small star spark in the corner — quick hint
    // that something special is going on even if the icon is tiny.
    if (item->rarity == ItemRarity::Legendary) {
        ImU32 spark = voxelToU32(rarityUiColor(item->rarity));
        ImVec2 sc(c.x + h * 0.7f, c.y - h * 0.7f);
        dl->AddCircleFilled(sc, h * 0.10f, spark, 8);
    }
}

// --- One cell of the inventory grid -----------------------------------
// Renders the cell background, item icon, drag-drop hooks, hover tooltip.
// `dragSrcIdx` is the index used for drag payloads (the same as `cellIdx`
// for grid cells; equip-slot cells pass the item's grid index here).
//
// Returns true if any action that needs the rig rebuilt happened on this
// cell this frame.
bool drawItemCell(AppContext& ctx, ImVec2 size, int cellIdx, Item* item,
                  int dragSrcIdx,
                  bool allowDropAsSwap = true,
                  EquipSlot equipDropTarget = EquipSlot::None) {
    ImGui::PushID(cellIdx + (int)equipDropTarget * 1000);

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Cell background — slightly darker than the window, tinted by rarity
    // when an item is present.
    ImU32 bg = IM_COL32(20, 14, 10, 220);
    ImU32 border = IM_COL32(110, 90, 50, 255);
    if (item) {
        Voxel rar = rarityUiColor(item->rarity);
        border = IM_COL32(rar.r, rar.g, rar.b, 255);
        bg     = IM_COL32(rar.r / 6, rar.g / 6, rar.b / 6, 220);
    }
    // Sharp pixel-art corners — no rounding, double border for the
    // "inset metal frame" look.
    dl->AddRectFilled(cursor, ImVec2(cursor.x + size.x, cursor.y + size.y), bg);
    dl->AddRect(cursor, ImVec2(cursor.x + size.x, cursor.y + size.y), border,
                0.0f, 0, item ? 2.0f : 1.0f);
    // Inner thin hairline for depth.
    ImU32 innerHi = IM_COL32(255, 220, 140, 50);
    dl->AddRect(ImVec2(cursor.x + 2, cursor.y + 2),
                ImVec2(cursor.x + size.x - 2, cursor.y + size.y - 2),
                innerHi, 0.0f, 0, 1.0f);

    if (item) {
        ImVec2 center(cursor.x + size.x * 0.5f, cursor.y + size.y * 0.5f);
        drawItemIcon(dl, center, size.x * 0.78f, item);

        // Equipped marker — small "E" badge bottom-right so the player can
        // tell what's already on without opening the loadout panel.
        if (ctx.inventory.isEquipped(item)) {
            ImVec2 p(cursor.x + size.x - 12.0f, cursor.y + size.y - 14.0f);
            dl->AddCircleFilled(ImVec2(p.x + 5, p.y + 7), 7.0f,
                                IM_COL32(200, 170, 50, 230), 10);
            dl->AddText(ImVec2(p.x + 1, p.y), IM_COL32(20, 14, 10, 255), "E");
        }

        // Level badge in the top-left. Red when the player can't equip
        // it yet, parchment when they can.
        bool ok = canEquipForLevel(item, ctx.playerLevel);
        ImU32 badgeBg = ok ? IM_COL32(40, 30, 20, 220) : IM_COL32(160, 30, 30, 230);
        ImU32 badgeFg = ok ? IM_COL32(240, 220, 160, 255) : IM_COL32(255, 220, 200, 255);
        ImVec2 bg0(cursor.x + 2, cursor.y + 2);
        ImVec2 bg1(cursor.x + 22, cursor.y + 14);
        dl->AddRectFilled(bg0, bg1, badgeBg);
        dl->AddRect(bg0, bg1, badgeFg, 0.0f, 0, 1.0f);
        char buf[8]; snprintf(buf, sizeof(buf), "%d", item->level);
        dl->AddText(ImVec2(cursor.x + 5, cursor.y + 1), badgeFg, buf);

        // Dim the whole icon for under-leveled items.
        if (!ok) {
            dl->AddRectFilled(cursor, ImVec2(cursor.x + size.x, cursor.y + size.y),
                              IM_COL32(0, 0, 0, 120));
        }
    }

    // An invisible button claims the cell's input so drag/click work.
    bool clicked = ImGui::InvisibleButton("##cell", size);
    bool changed = false;

    // Hover tooltip.
    if (item && ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImVec4 nameCol = voxelToImColor(rarityUiColor(item->rarity));
        ImGui::TextColored(nameCol, "%s", item->getName().c_str());
        ImGui::TextDisabled("%s  -  %s",
                            rarityName(item->rarity),
                            itemSubtitle(item).c_str());
        if (!item->setKey.empty()) {
            ImGui::TextColored(ImVec4(0.65f, 0.95f, 0.95f, 1.0f),
                               "Set: %s", item->setKey.c_str());
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Right-click toggles equip / Drag to slot / Q to drop");
        bool levelOk = canEquipForLevel(item, ctx.playerLevel);
        if (levelOk) {
            ImGui::Text("Level: %d", item->level);
        } else {
            ImGui::TextColored(ImVec4(1.00f, 0.40f, 0.35f, 1.0f),
                               "Requires Level %d (you are %d)",
                               item->level, ctx.playerLevel);
        }
        if (item->attackPower  > 0) ImGui::Text("Attack:  %.0f", item->attackPower);
        if (item->defenseValue > 0) ImGui::Text("Defense: %.0f", item->defenseValue);
        if (ctx.inventory.isEquipped(item))
            ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.40f, 1.0f), "(equipped)");
        ImGui::EndTooltip();
    }

    // Drag SOURCE — only meaningful if there's something here. Uses the
    // grid index of the item (not the cell, in case the cell is an equip
    // slot showing an item that lives elsewhere in the grid).
    if (item && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        ImGui::SetDragDropPayload(INV_DRAG_TYPE, &dragSrcIdx, sizeof(int));
        // Preview shown attached to the cursor while dragging.
        drawItemIcon(ImGui::GetWindowDrawList(),
                     ImVec2(ImGui::GetCursorScreenPos().x + 20,
                            ImGui::GetCursorScreenPos().y + 20),
                     32, item);
        ImGui::Text("  %s", item->getName().c_str());
        ImGui::EndDragDropSource();
    }

    // Drag TARGET — accepts a grid-index payload.
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(INV_DRAG_TYPE)) {
            int srcIdx = *static_cast<const int*>(p->Data);
            if (equipDropTarget != EquipSlot::None) {
                // Dropping onto an equip slot — only if the slot accepts
                // the item's kind. Weapons cross-fit for the off-hand
                // when they're shields, and main-hand for everything else.
                Item* dragged = ctx.inventory.at(srcIdx);
                bool fits = false;
                if (dragged) {
                    if (dragged->getSlot() == equipDropTarget) fits = true;
                    else if (equipDropTarget == EquipSlot::OffHand
                             && dragged->getKind() == ItemKind::Weapon
                             && static_cast<WeaponItem*>(dragged)->getType() == WeaponType::Shield)
                        fits = true;
                }
                if (fits && !canEquipForLevel(dragged, ctx.playerLevel)) {
                    // Reject equip — surface a toast so the player isn't
                    // left wondering why the slot didn't fill.
                    AppContext::HudToast t;
                    t.text  = "Requires Level " + std::to_string(dragged->level);
                    t.color = {220, 100, 80, 255};
                    t.lifeTime = 2.5f;
                    ctx.toasts.push_back(std::move(t));
                    fits = false;
                }
                if (fits) {
                    ctx.inventory.equip(dragged, equipDropTarget);
                    changed = true;
                }
            } else if (allowDropAsSwap) {
                // Dropping onto a regular grid cell — swap (handles
                // empty cells too: swap with null moves the source).
                ctx.inventory.swap(srcIdx, cellIdx);
                changed = true;
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Q while hovering = drop this item into the world. Sends a request
    // to the server which then broadcasts a LootSpawn so every player
    // sees the drop and can pick it up — this is how items get shared
    // between players.
    if (item && ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey_Q)) {
        if (ctx.client && ctx.client->connected) {
            DropItemRequestPacket dp {};
            // Drop at the player's feet plus a tiny scatter so multiple
            // dropped items don't all stack on the exact same voxel.
            dp.x = ctx.camera.position.x + ((rand() % 100) - 50) / 200.0f;
            dp.y = ctx.camera.position.y + 0.4f;
            dp.z = ctx.camera.position.z + ((rand() % 100) - 50) / 200.0f;
            dp.kind  = (item->getKind() == ItemKind::Clothing) ? 1
                     : (item->getKind() == ItemKind::Weapon)   ? 2 : 0;
            dp.rarity = (uint8_t)item->rarity;
            dp.level  = item->level;
            if (item->getKind() == ItemKind::Clothing) {
                auto* c = static_cast<ClothingItem*>(item);
                dp.subtype     = (uint8_t)c->getTier();
                dp.slot        = (uint8_t)c->getSlot();
                dp.primary     = c->primaryColor;
                dp.accent      = c->accentColor;
                dp.patternSeed = c->patternSeed;
            } else if (item->getKind() == ItemKind::Weapon) {
                auto* w = static_cast<WeaponItem*>(item);
                dp.subtype     = (uint8_t)w->getType();
                dp.slot        = (uint8_t)w->getSlot();
                dp.primary     = w->primaryColor;
                dp.accent      = w->accentColor;
                dp.patternSeed = 0;
                dp.element     = (uint8_t)w->element;
            }
            dp.attackPower  = item->attackPower;
            dp.defenseValue = item->defenseValue;
            std::memset(dp.name,    0, sizeof(dp.name));
            std::memset(dp.setName, 0, sizeof(dp.setName));
            std::memcpy(dp.name, item->getName().c_str(),
                        std::min(item->getName().size(), sizeof(dp.name) - 1));
            std::memcpy(dp.setName, item->setKey.c_str(),
                        std::min(item->setKey.size(), sizeof(dp.setName) - 1));
            ctx.client->send(PacketType::DropItemRequest, &dp, sizeof(dp));
            // Remove the item from our own inventory immediately — the
            // server will broadcast the LootSpawn back and we'll see
            // the drop appear in the world like everyone else.
            ctx.inventory.removeItem(item);
            changed = true;
        }
    }

    // Right-click on an equipped item = quick unequip.
    if (item && ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        if (ctx.inventory.isEquipped(item)) {
            auto rev = ctx.inventory.equippedIds().find(item->getId());
            if (rev != ctx.inventory.equippedIds().end()) {
                ctx.inventory.unequip(rev->second);
                changed = true;
            }
        } else if (canEquipForLevel(item, ctx.playerLevel)) {
            ctx.inventory.equip(item);
            changed = true;
        } else {
            AppContext::HudToast t;
            t.text  = "Requires Level " + std::to_string(item->level);
            t.color = {220, 100, 80, 255};
            t.lifeTime = 2.5f;
            ctx.toasts.push_back(std::move(t));
        }
    }
    (void)clicked;

    ImGui::PopID();
    return changed;
}

// Empty equip slot rendered as a placeholder cell. Accepts drops of
// matching slot type.
bool drawEquipSlotCell(AppContext& ctx, ImVec2 pos, ImVec2 size,
                       EquipSlot slot, const char* label) {
    ImGui::SetCursorPos(pos);
    Item* eq = ctx.inventory.equipped(slot);
    int dragSrc = eq ? ctx.inventory.indexOf(eq) : -1;
    bool changed = drawItemCell(ctx, size, /*cellIdx*/-1, eq, dragSrc,
                                /*allowDropAsSwap*/false, slot);

    // Label below the cell — helps the player learn which slot is which.
    ImVec2 cur = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    (void)dl;
    // We use the window's drawlist so it stays inside the window scissor.
    ImGui::SetCursorPos(ImVec2(pos.x, pos.y + size.y + 2));
    ImGui::TextDisabled("%s", label);
    (void)cur;
    return changed;
}

void afterEquipmentChange(AppContext& ctx) {
    if (ctx.playerRig) {
        rebuildRigFromInventory(*ctx.playerRig, ctx.inventory);
        sendPlayerModelUpdate(ctx);
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Public entry points (declared in ui.h)
// ---------------------------------------------------------------------------

void renderInventoryUI(AppContext& ctx, GLFWwindow* window) {
    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    if (fbW <= 0) fbW = WINDOW_WIDTH;
    if (fbH <= 0) fbH = WINDOW_HEIGHT;

    const int   cols = 6;
    const int   rows = 4;
    const float cellSize = 56.0f;
    const float pad      = 6.0f;
    const float gridW    = cols * cellSize + (cols - 1) * pad;
    const float winW     = gridW + 30.0f;
    const float winH     = rows * cellSize + (rows - 1) * pad + 150.0f;

    // Inventory panel sits on the right; character loadout sits on the
    // left (see renderCharacterLoadoutUI). They line up vertically.
    float winX = (fbW * 0.5f) + 20.0f;
    float winY = (fbH - winH) * 0.5f;

    ImGui::SetNextWindowPos(ImVec2(winX, winY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Always);
    ImGui::Begin("Inventory", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoCollapse);

    int filled = 0;
    for (int i = 0; i < ctx.inventory.capacity(); i++)
        if (ctx.inventory.at(i)) filled++;
    ImGui::TextColored(ImVec4(0.96f, 0.85f, 0.5f, 1.0f),
                       "Bag  %d / %d", filled, ctx.inventory.capacity());

    // Procedural generation buttons — same as before but slimmer now.
    // Pass the player's level so generated gear stays equippable.
    if (ImGui::SmallButton("Random")) {
        ctx.inventory.addItem(
            generateRandomItem(std::random_device{}(), ctx.playerLevel));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clothing")) {
        ctx.inventory.addItem(
            generateRandomClothing(std::random_device{}(), ctx.playerLevel));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Weapon")) {
        ctx.inventory.addItem(
            generateRandomWeapon(std::random_device{}(), ctx.playerLevel));
    }
    ImGui::TextDisabled("Drag to reorder, drag onto a slot to equip, right-click to toggle.");

    ImGui::Separator();

    // 6x4 grid of cells.
    bool changed = false;
    ImVec2 gridOrigin = ImGui::GetCursorPos();
    for (int r = 0; r < rows; r++) {
        for (int col = 0; col < cols; col++) {
            int idx = r * cols + col;
            ImGui::SetCursorPos(ImVec2(gridOrigin.x + col * (cellSize + pad),
                                       gridOrigin.y + r   * (cellSize + pad)));
            Item* it = ctx.inventory.at(idx);
            if (drawItemCell(ctx, ImVec2(cellSize, cellSize), idx, it, idx))
                changed = true;
        }
    }
    ImGui::SetCursorPos(ImVec2(gridOrigin.x,
                               gridOrigin.y + rows * (cellSize + pad) + 6));

    if (ImGui::Button("Close (I / C / Esc)", ImVec2(-1, 28))) {
        ctx.showInventory = false;
        ctx.showCharacterLoadout = false;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        ctx.firstMouse = true;
    }
    ImGui::End();

    if (changed) afterEquipmentChange(ctx);
}

void renderCharacterLoadoutUI(AppContext& ctx, GLFWwindow* window) {
    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(window, &fbW, &fbH);
    if (fbW <= 0) fbW = WINDOW_WIDTH;
    if (fbH <= 0) fbH = WINDOW_HEIGHT;

    const float cellSize = 60.0f;
    const float winW = 320.0f;
    const float winH = 480.0f;
    float winX = (fbW * 0.5f) - winW - 20.0f;
    float winY = (fbH - winH) * 0.5f;

    ImGui::SetNextWindowPos(ImVec2(winX, winY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Always);
    ImGui::Begin("Character Loadout", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoCollapse);

    ImGui::TextColored(ImVec4(0.96f, 0.85f, 0.5f, 1.0f), "%s", ctx.playerName);
    ImGui::Separator();

    // Humanoid layout — slots positioned roughly where they sit on the
    // body. Coordinates are in window-content space (after the title bar).
    //
    //                    +--------+
    //                    | Helmet |
    //                    +--------+
    //                    +--------+
    //                    |Should. |
    //                    +--------+
    //          +--------+ +-------+ +--------+
    //          |MainHand| | Chest | |OffHand |
    //          +--------+ +-------+ +--------+
    //                    +--------+
    //                    |  Legs  |
    //                    +--------+
    //                    +--------+
    //                    |  Feet  |
    //                    +--------+
    const float colCenter = (winW - cellSize) * 0.5f;
    const float colLeft   = colCenter - cellSize - 16.0f;
    const float colRight  = colCenter + cellSize + 16.0f;
    const float startY    = 42.0f;
    const float rowGap    = cellSize + 18.0f;

    bool changed = false;
    auto slot = [&](ImVec2 pos, EquipSlot s, const char* label) {
        if (drawEquipSlotCell(ctx, pos, ImVec2(cellSize, cellSize), s, label))
            changed = true;
    };

    slot(ImVec2(colCenter, startY + rowGap * 0), EquipSlot::Helmet,    "Helmet");
    slot(ImVec2(colCenter, startY + rowGap * 1), EquipSlot::Shoulders, "Shoulders");
    slot(ImVec2(colLeft,   startY + rowGap * 2), EquipSlot::MainHand,  "Main Hand");
    slot(ImVec2(colCenter, startY + rowGap * 2), EquipSlot::Chest,     "Chest");
    slot(ImVec2(colRight,  startY + rowGap * 2), EquipSlot::OffHand,   "Off Hand");
    slot(ImVec2(colCenter, startY + rowGap * 3), EquipSlot::Legs,      "Legs");
    slot(ImVec2(colCenter, startY + rowGap * 4), EquipSlot::Feet,      "Feet");

    // Aggregate stats at the bottom.
    ImGui::SetCursorPos(ImVec2(16.0f, startY + rowGap * 5 + 12.0f));
    float defense = 0.0f, attack = 0.0f;
    for (int i = 0; i < ctx.inventory.capacity(); i++) {
        Item* it = ctx.inventory.at(i);
        if (!it || !ctx.inventory.isEquipped(it)) continue;
        defense += it->defenseValue;
        auto rev = ctx.inventory.equippedIds().find(it->getId());
        if (rev != ctx.inventory.equippedIds().end()
            && rev->second == EquipSlot::MainHand)
            attack += it->attackPower;
    }
    ImGui::Text("Attack:  %.0f", attack);
    ImGui::Text("Defense: %.0f", defense);
    ImGui::TextDisabled("Drag from the bag to a slot, or right-click an item.");

    ImGui::End();

    if (changed) afterEquipmentChange(ctx);
}
