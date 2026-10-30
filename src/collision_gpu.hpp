#pragma once
#include <d3d11.h>
#include <imgui.h>
#include <cstdint>
#include <vector>

// GPU drawing for the world-collision view: normally injected right after the game
// composites its scene to the backbuffer (before the HUD), else at Present before ImGui.
// A private depth buffer makes collision faces occlude each other exactly: pass 1
// writes only depth, pass 2 colours only the nearest surface (no stacking at any
// opacity), then edges draw slightly toward the eye.
namespace collision_gpu {
// Clip-space position (x,y,z,w) as built by the overlay, and an IM_COL32 colour.
struct Vertex {float x=0,y=0,z=0,w=1;std::uint32_t color=0;};
struct Batch {
    std::vector<Vertex> fills,lines;
    // Drawn after the collision without any depth test (NAV graph); the scene-depth
    // test still lets zombies and the weapon cover it.
    std::vector<Vertex> overlay_fills,overlay_lines;
    float viewport_x=0,viewport_y=0,viewport_width=0,viewport_height=0;
    void clear() {fills.clear();lines.clear();overlay_fills.clear();overlay_lines.clear();}
    bool empty() const {return fills.empty()&&lines.empty()&&overlay_fills.empty()&&overlay_lines.empty();}
};
// Projection used for the private depth buffer (view-space units).
inline constexpr float near_plane=4,far_plane=16384;
// Creates shaders and states on first use; false means use the ImGui fallback.
bool ready(ID3D11Device* device);
// Optional test against the game's scene depth (reversed, infinite: distance =
// near_plane / depth). Where the scene is nearer by more than max(tolerance,
// relative * distance), the shader rebuilds that scene point in world space and drops
// the collision fragment only if the point is closer than weapon_distance (weapon and
// hands) or inside one of the boxes: living zombies first (box_count), then 3D models
// (model_count: props, script models, vehicles; user request 2026-10-30). World walls
// and floors never cover it. World overlay lists use the zombie boxes only: they have
// no depth to compare, so a loose prop box would hide them wholesale.
struct Occlusion {
    ID3D11DepthStencilView* scene=nullptr;
    // The viewmodel is drawn squeezed against the near plane: live probe at near 4 read
    // depth 0.9845 on the staff (4.06 units) against 0.0274 (146 units) on the world.
    // The overlay sets weapon_distance to 1.5 near planes.
    float near_plane=4,tolerance=8,relative=0.03f,weapon_distance=6;
    // Inside model boxes a finer margin, so props resting on a floor or against a wall
    // (rope posts, a bookshelf) cover the collision too (user screenshots 2026-10-30).
    float model_tolerance=1,model_relative=0.004f;
    // "Models on top" (user request 2026-10-30): 0 the box rules above; 1 the model mask
    // (mask_begin): no box tests, never on the weapon; on a model pixel the collision is
    // dropped, or drawn at behind_strength of its opacity when clearly in front of the model
    // (by max(tolerance, relative * distance)), so models behind clips stay visible.
    int mode=0;float behind_strength=0.35f;
    // Camera that rendered the scene: origin, forward, left, up; tan of the half FOVs.
    float origin[3]{},forward[3]{},left[3]{},up[3]{},tan_x=1,tan_y=1;
    static constexpr int max_boxes=256;
    int box_count=0,model_count=0;float boxes[max_boxes][6]{};  // world lo xyz, hi xyz
};
// Draws into target with a private depth buffer of width x height. Saves and
// restores all pipeline state it changes (output-merger targets excepted: the caller
// restores those). Returns whether the scene-depth test was active.
bool draw(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11RenderTargetView* target,unsigned width,unsigned height,const Batch& batch,const Occlusion* occlusion=nullptr);
// Draws an ImGui draw list (positions in target pixels; texture ids are SRVs) with the
// same weapon and zombie occlusion, no depth test. camera_viewport is the game camera's
// viewport in target pixels (x, y, width, height), for rebuilding scene points.
bool draw_list(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11RenderTargetView* target,unsigned width,unsigned height,
    const ImDrawList& list,const float camera_viewport[4],const Occlusion* occlusion=nullptr);
// The model mask target (R8, width x height, cleared when clear is set), the pixel shader
// that marks it and the depth state that keeps a replayed draw to its visible pixels.
struct MaskPass {ID3D11RenderTargetView* target=nullptr;ID3D11PixelShader* shader=nullptr;ID3D11DepthStencilState* depth=nullptr;};
bool mask_begin(ID3D11Device* device,ID3D11DeviceContext* context,unsigned width,unsigned height,bool clear,MaskPass& out);
void release();
}
