#pragma once
#include <d3d11.h>

// Pass-through hooks on the game's immediate D3D11 context (target binds and draws).
// Nothing the game does is changed. They find the scene depth (the backbuffer-sized
// depth view that received the most draws last frame) and run the injection: the
// overlay's under-HUD drawing, right after the game's first draw onto the backbuffer
// each frame (its scene composite, before any HUD draw).
namespace frame_trace {
bool install(ID3D11DeviceContext* context);
void uninstall();
// Brackets the overlay's own D3D11 calls so they are not counted.
void overlay_begin();
void overlay_end();
// Ends the game's frame (call at Present before the overlay draws).
void on_present(unsigned backbuffer_width,unsigned backbuffer_height);
// The callback's own D3D11 calls are excluded. Pass null to disable.
using Inject=void(*)();
void set_injection(Inject callback,ID3D11Resource* backbuffer);
bool injected_last_frame();
// Model mask (user request 2026-10-30, "Models on top"). The scene's colour pass draws the
// map geometry from one vertex buffer of 36-byte vertices, then every model (zombies,
// corpses, props placed as models, the weapon) with 32-byte vertices (research/drawtrace
// captures). With the mask on, each opaque model colour draw on the scene depth is replayed into
// collision_gpu's model mask with the game's vertex shader and depth.
void set_model_mask(bool on);
// Whether the mask was written (and cleared first) in the frame now being drawn.
bool mask_written();
// Scene depth candidate from the last frame, AddRef'd (caller releases), or null.
ID3D11DepthStencilView* scene_depth();
}
