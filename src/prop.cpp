#include "prop.h"
#include "prop_builders.h"
#include "voxel_model.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

void voxFill(VoxelVolume* v, int x0, int y0, int z0,
             int x1, int y1, int z1, Voxel c) {
    if (!v) return;
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    if (z0 > z1) std::swap(z0, z1);
    x0 = std::max(0, x0); y0 = std::max(0, y0); z0 = std::max(0, z0);
    x1 = std::min(v->sizeX - 1, x1);
    y1 = std::min(v->sizeY - 1, y1);
    z1 = std::min(v->sizeZ - 1, z1);
    for (int z = z0; z <= z1; z++)
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++)
                v->setVoxel(x, y, z, c);
}

// --- PropLibrary -----------------------------------------------------------

PropLibrary::~PropLibrary() { destroy(); }

void PropLibrary::destroy() {
    for (auto& v : volumes) { delete v; v = nullptr; }
    isBuilt = false;
}

void PropLibrary::buildAll() {
    if (isBuilt) return;
    volumes[(int)PropType::Bookshelf]   = buildBookshelf();
    volumes[(int)PropType::Bed]         = buildBed();
    volumes[(int)PropType::Lantern]     = buildLanternProp();
    volumes[(int)PropType::Cooker]      = buildCooker();
    volumes[(int)PropType::Table]       = buildTable();
    volumes[(int)PropType::Chair]       = buildChair();
    volumes[(int)PropType::Crockery]    = buildCrockery();
    volumes[(int)PropType::StreetLamp]  = buildStreetLamp();
    volumes[(int)PropType::PottedPlant] = buildPottedPlant();
    volumes[(int)PropType::Bush]        = buildBush();
    volumes[(int)PropType::Bench]       = buildBench();
    volumes[(int)PropType::Fence]       = buildFenceSection();
    for (auto* v : volumes)
        if (v) v->updateMesh();
    isBuilt = true;
}

VoxelVolume* PropLibrary::mesh(PropType t) const {
    int i = (int)t;
    if (i < 0 || i >= (int)PropType::Count) return nullptr;
    return volumes[i];
}

// --- Prop ------------------------------------------------------------------

Prop::Prop(PropType t, glm::vec3 pos, float yawDeg, const PropLibrary* lib)
    : GameObject(ObjectKind::Prop), type(t), library(lib) {
    position = pos;
    yaw      = yawDeg;
}

void Prop::update(float, World&) {}   // props are static

void Prop::draw(GLuint modelLoc) const {
    if (!library) return;
    VoxelVolume* v = library->mesh(type);
    if (!v) return;
    // position is the bottom-centre on the ground; the grid is filled from
    // (0,0,0), so centre it in X/Z and rest it on y = 0.
    glm::mat4 m = baseMatrix(PROP_SCALE);
    m = glm::translate(m, glm::vec3(-v->sizeX * 0.5f, 0.0f, -v->sizeZ * 0.5f));
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, &m[0][0]);
    v->draw();
}

void Prop::getAABB(glm::vec3& mn, glm::vec3& mx) const {
    float hx = 0.5f, hy = 1.0f, hz = 0.5f;
    if (library) {
        VoxelVolume* v = library->mesh(type);
        if (v) {
            hx = v->sizeX * PROP_SCALE * 0.5f;
            hy = v->sizeY * PROP_SCALE;
            hz = v->sizeZ * PROP_SCALE * 0.5f;
        }
    }
    mn = glm::vec3(position.x - hx, position.y,      position.z - hz);
    mx = glm::vec3(position.x + hx, position.y + hy, position.z + hz);
}
