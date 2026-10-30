#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "navigation.hpp"

// World collision the player character meets: floors, walls and roofs.
// Read-only native decode of the loaded clipMap (reverse-engineered layout,
// evidence in docs/world-collision-readonly-20261012.md):
//  - world brushes: every brush no entity cmodel owns, with contents SOLID (0x1)
//    or PLAYERCLIP (0x10000); faces rebuilt from bounds + side planes;
//  - collision triangles: vertices 0x25038EC/F0, u16 index triples 0x25038F4/F8
//    (outdoor floors measured at +0.19 units under the player's feet), minus the
//    triangles entity cmodels own through their leaf AABB trees (model-local space);
//  - triangle materials: materials 0x2503890/94 (stride 12: name, surface flags,
//    contents), partitions 0x2503900/04 (stride 16, live-verified: u8 triangle count, int first
//    triangle), AABB tree 0x2503908/0C (stride 32: u16 material +12, u16 child count
//    +14, leaf partition +28). Layout from raw dumps; validated at build time.
//  - entity cmodels (brush models: doors, elevators, bus clips): the same brushes and
//    triangles, kept in model-local space and posed at the live entity (movers()).
// Not decoded: XModel collision (static props, script models, vehicles).
namespace collision {
enum class Kind : std::uint8_t {floor,wall,roof,clip};
// Walkable-slope convention: normal z >= 0.7 floor, <= -0.7 roof, else wall.
inline Kind orientation(nav::Vec3 normal) {return normal.z>=0.7f?Kind::floor:normal.z<=-0.7f?Kind::roof:Kind::wall;}
// Convex polygon with its outward normal (brush faces point out of the brush;
// triangles are re-wound to point up out of floors) and centroid for sorting.
// blocking is false for triangles whose collision material lacks SOLID/PLAYERCLIP.
// source is the brush index or the triangle index; material is the triangle's
// collision material (-1 for brushes or unmapped triangles).
// moving: posed from an entity this frame (movers()); bounds: a model entity's box,
// standing in for the model collision Gnosis does not decode.
struct Face {std::vector<nav::Vec3> points;nav::Vec3 normal,centre,lo,hi;Kind kind=Kind::wall;bool triangle=false,blocking=true;
    std::uint32_t source=0,contents=0;int material=-1;bool moving=false,bounds=false;};
struct MaterialUse {std::string name;std::uint32_t contents=0;unsigned triangles=0;};
inline constexpr float cell_size=512;
inline std::int64_t cell_key(float x,float y) {
    const auto ix=static_cast<std::int64_t>(std::floor(x/cell_size))+(1<<20),iy=static_cast<std::int64_t>(std::floor(y/cell_size))+(1<<20);
    return (ix<<32)|iy;
}
struct Snapshot {
    std::string map;
    std::vector<Face> faces;
    // Each face is listed in every cell its XY bounds overlap; faces spanning more
    // than 64 cells go in large instead and are always considered.
    std::unordered_map<std::int64_t,std::vector<std::uint32_t>> cells;
    std::vector<std::uint32_t> large;
    unsigned world_brushes=0,entity_brushes=0,other_contents=0,skipped_brushes=0,triangles=0,entity_triangles=0;
    // Triangle materials (AABB-tree leaf -> partition -> triangles). materials_ok is
    // false when the mapping failed validation; then every triangle counts as blocking.
    bool materials_ok=false;unsigned nonblocking_triangles=0;std::string material_status;
    std::vector<MaterialUse> nonblocking;
    std::vector<std::string> material_names;
    // Player-blocking brush and triangle faces of each entity cmodel, in model-local space
    // (index = cmodel handle; 0, the world, stays empty). movers() poses them.
    std::vector<std::vector<Face>> models;
    // Static props' world bounds (clipMap static models 0x25038D4/D8, stride 84: origin
    // +12, axes +24, absmin +60, absmax +72; live TranZit 2026-10-30, 5747 props).
    std::vector<std::pair<nav::Vec3,nav::Vec3>> props;
};
// A 3D model's world box: a static prop or a model entity (script model, vehicle, barrier).
struct ModelBox {nav::Vec3 lo,hi;float distance=0;};
// Builds only after enable(true); the first build per map runs once on the game thread.
void enable(bool on);
void update();
void clear();
std::shared_ptr<const Snapshot> snapshot();
// Moving collision (user request 2026-10-30: elevators, bus clips): the cmodel faces of
// every live, player-blocking brush-model entity within radius + 2048 of eye, at its
// current origin and angles, in world space. Returns the entities posed. Render thread.
// With model_bounds, solid model entities (script_model, script_vehicle, zbarrier_*;
// entity contents with SOLID) add their oriented entity box (local mins/maxs +0x100/+0x10C,
// posed like a cmodel) as an approximation of their model collision (user choice
// 2026-10-30; the bus reads -96,-91,0 .. 449,88,172 live).
unsigned movers(const Snapshot& s,nav::Vec3 eye,float radius,std::vector<Face>& out,bool model_bounds=false);
// The boxes of 3D models within radius of eye, nearest first, at most limit: static props
// plus live model entities (script_model, script_vehicle, zbarrier_*; engine absmin/absmax
// +0x11C/+0x128). They let models cover the collision view as zombies do. Render thread.
void model_boxes(const Snapshot& s,nav::Vec3 eye,float radius,std::size_t limit,std::vector<ModelBox>& out);
const char* status();
}
