#pragma once
// Declarations shared by the overlay's source files. overlay.hpp is the public part
// (startup and shutdown), this header is internal to them:
//   overlay.cpp           renderer hooks (Present, ResizeBuffers, window input), the frame
//                         loop and the in-frame injection
//   overlay_settings.cpp  options, T6Gnosis.cfg and keyboard input (menu key, quick controls)
//   overlay_menu.cpp      the F8 menu
//   overlay_hud.cpp       start-up hint and movement HUD
//   overlay_world.cpp     world drawings: NAV, spawns, triggers, collision, zombie markers
//   overlay_aids.cpp      map aids: Origins staffs and robot footsteps, Mob of the Dead, TranZit Avogadro,
//                         thrown grenades
#include <d3d11.h>
#include <imgui.h>
#include <array>
#include <cstdint>
#include <memory>
#include "navigation.hpp"
#include "spawns.hpp"
#include "triggers.hpp"
#include "despawn_overlay.hpp"
#include "collision_gpu.hpp"
#include "world_collision.hpp"
#include "staff_aid.hpp"
#include "mob_aid.hpp"
#include "avogadro_aid.hpp"
#include "grenade_aid.hpp"
#include "robot_aid.hpp"

namespace overlay
{
    // ---- Options (overlay_settings.cpp) ----

    // Every user option of the overlay. All are saved in T6Gnosis.cfg (the cfg key names are
    // listed in overlay_settings.cpp). Defaults (user requests 2026-10-13): every drawing off
    // except the movement HUD. The Origins aids are on but draw on Origins only.
    struct Settings
    {
        bool visible = true; // master switch for everything Gnosis draws

        // Movement HUD
        bool stamina_bar = true, speedometer = true, jump_stats = true;

        // Zombies. The despawn tracker keeps its own options in despawn::ui::settings.
        bool wallhack = false;

        // Spawns
        bool spawn_points = false, spawn_labels = false, special_spawns = false, active_spawns_only = false;
        float spawn_range = 2000;

        // Player collision. Collision draws as high and low as wide (2026-10-30): with the usual
        // radius/3 height an overhead sign board vanished while its posts stayed. Default radius
        // 1000 (user request).
        bool world_collision = false, zombie_boxes = false, player_bounds = false;
        float collision_range = 1000, collision_opacity = 0.85f;
        // "Models on top": collision is drawn inside the game's frame, after the world and before
        // any model, so every model covers it with the game's own depth (frame_trace). Off: the
        // older drawing after the frame with the box mask.
        bool collision_in_frame = true;

        // Triggers
        bool trigger_geometry = false, trigger_labels = false, playable_area = false;
        float trigger_range = 2000;

        // NAV graph
        bool nav_nodes = false, nav_links = false, nav_ids = false, nav_directions = false;
        float nav_range = 800;

        // Origins puzzle aids
        bool wind_aid = true, fire_aid = true, wind_details = false, fire_details = false;
        // Origins giant robot footsteps: the 1500-unit radius around each foot inside which a big
        // footstep sends Samantha's wind staff clue (off by default: large rings, practice only),
        // and a readout of each step and the clue's wait. No rumble rings (user decision 2026-11-18:
        // only what affects gameplay, more rings were confusing).
        bool robot_steps = false, robot_details = true;

        // Mob of the Dead: blue-skull aid, Afterlife shock boxes and Afterlife ghost spawn spots
        // (with mob_afterlife_only, just the spot or spots used now).
        bool mob_skulls = false, mob_shock_boxes = false, mob_afterlife_spawns = false, mob_afterlife_only = false;
        float mob_afterlife_range = 1500;

        // TranZit Avogadro aid and its drawings. User requests 2026-10-30: his lines over his
        // head only within avogadro_range (else at the top of the screen), an option to keep them
        // at the top always, and his spawn locations.
        bool avogadro_aid = false, avogadro_dashes = true, avogadro_route = true, avogadro_locked = false, avogadro_spawns = false;
        float avogadro_range = 1500;

        // Thrown grenades (frag, EMP, G-strike): predicted path of the one held back, else of the one
        // carried that the priority prefers (lethal frag, or tactical EMP / G-strike), and the last
        // real throw.
        bool grenade_arcs = false;
        bool grenade_tactical_first = false;

        // Menu window placement, -1 until first saved (then the default placement is used).
        float menu_x = -1, menu_y = -1, menu_w = -1, menu_h = -1;
    };

    extern Settings settings;

    // One radius per feature (user request 2026-10-13): the vertical extent is a fixed share
    // of the horizontal radius.
    inline constexpr float vertical_share = 1.0f / 3;

    inline float vertical(float radius) { return radius * vertical_share; }

    // Quick controls: one key per major top-level toggle (user request 2026-10-20), in menu
    // order. setting is the cfg key name (keep its spelling: saved binds use it).
    struct QuickBinding
    {
        const char* label;
        const char* setting;
        bool* value;
        int key = 0;
    };

    extern std::array<QuickBinding, 22> quick;

    // T6Gnosis.cfg beside the DLL. load_settings runs on the first frame (it records the
    // defaults for reset_settings), save_settings writes only when something changed.
    bool settings_loaded();
    void load_settings();
    void save_settings();
    // Restores every option and quick-control key and asks the menu to reset its placement.
    void reset_settings();
    // True once after reset_settings (the menu then uses its default placement).
    bool take_window_reset();

    // Keyboard, polled at Present while the game window has focus. key_pressed is edge
    // triggered (true on the frame the key goes down).
    bool key_pressed(int key);
    void forget_keys();
    // Quick-control keys while the menu is closed, or the key for the binding being captured.
    void handle_quick_keys(bool menu_is_open);
    // Binding capture from the Quick controls list: index of the binding waiting for a key, or -1.
    void start_key_capture(int index);
    int key_capture();
    void cancel_key_capture();
    // Display name of a virtual key (blank for none).
    void key_label(int key, char* out, int size);

    // ---- Frame state (overlay.cpp) ----

    // What one frame draws from: the map graph, the camera and fresh reader snapshots.
    struct FrameInfo
    {
        std::shared_ptr<const nav::Graph> graph;
        nav::Camera camera{};
        bool camera_valid = false;
        std::shared_ptr<const spawns::Snapshot> spawn_snapshot;
        bool spawn_valid = false;
        unsigned spawn_count = 0; // spawn sites drawn, for the Spawns bar in the menu
        std::shared_ptr<const triggers::Snapshot> trigger_snapshot;
        triggers::PlayerBox player{};
        bool player_valid = false;
        despawn::ui::View despawn_view;
    };

    // Model-entity boxes (approximate model collision) are switched off: the user found them
    // not useful (2026-10-30). collision::movers keeps the code behind its model_bounds flag.
    inline constexpr bool collision_model_bounds = false;
    // Moving brush collision (doors, elevators, bus clips) is switched off too (user request
    // 2026-10-30: the grey boxes are not useful). collision::movers keeps the code.
    inline constexpr bool collision_moving = false;

    // Strength of everything drawn behind the pause menu (user choice 2026-10-13).
    inline constexpr float paused_strength = 0.5f;

    extern ID3D11Device* device;
    extern bool menu_open;
    // The pause menu is open this frame (drawings fade to paused_strength).
    extern bool frame_paused;
    // The last injection drew the NAV graph on the GPU, under zombies (draw() then skips its
    // ImGui copy). in_injection is set while the injection runs draw().
    extern bool nav_injected, in_injection;
    // Collision drawn at Present when the injection did not run: built in draw(), submitted by
    // render() before ImGui. collision_gpu_failed: the GPU pass is unavailable and draw() falls
    // back to ImGui polygons.
    extern collision_gpu::Batch collision_batch;
    extern bool collision_gpu_pass, collision_gpu_failed;

    // Render pixels to backbuffer pixels (the refdef viewport can be smaller than the backbuffer).
    inline ImVec2 screen(nav::Point p, float sx, float sy) { return {p.x * sx, p.y * sy}; }

    // ---- Drawing ----

    // overlay_menu.cpp
    void menu(const FrameInfo& frame);

    // overlay_hud.cpp
    void draw_hint(std::uint64_t now, float height);
    void draw_movement_hud(float width, float height);

    // overlay_world.cpp. world: the in-world drawings (NAV, spawns, triggers, aids, zombie
    // markers), since 2026-10-14 normally drawn by the injection under the weapon and HUD.
    // present_part: the parts drawn at Present (collision fallback, Origins aids, Avogadro text).
    void draw(ImDrawList* list, float width, float height, FrameInfo& frame, bool world, bool present_part);
    // GPU batches for the injection and the Present fallback (collision_gpu).
    void build_collision(const nav::Camera& cam, const collision::Snapshot& s, const std::vector<collision::Face>& moving, float sx,
                         float sy, collision_gpu::Batch& batch);
    void build_zombie_boxes(const nav::Camera& cam, collision_gpu::Batch& batch);
    void build_nav(const nav::Camera& cam, const nav::Graph& graph, float sx, float sy, collision_gpu::Batch& batch);

    // overlay_aids.cpp
    void draw_wind(ImDrawList* list, const nav::Camera& cam, const staff::View& view, float width, float height, float sx, float sy,
                   bool details);
    void draw_fire(ImDrawList* list, const nav::Camera& cam, const staff::View& view, float width, float height, float sx, float sy,
                   bool details);
    void draw_mob(ImDrawList* list, const nav::Camera& cam, const mob::View& view, float height, float sx, float sy);
    void draw_grenades(ImDrawList* list, const nav::Camera& cam, const grenade::View& view, float sx, float sy);
    void draw_robot_world(ImDrawList* list, const nav::Camera& cam, const robot::View& view, float sx, float sy);
    void draw_robot_text(ImDrawList* list, const robot::View& view, float width, float height);
    void draw_avogadro_world(ImDrawList* list, const nav::Camera& cam, const nav::Graph& g, const avogadro::View& view, float label_size,
                             float sx, float sy);
    void draw_avogadro_text(ImDrawList* list, const nav::Camera& cam, const nav::Graph& g, const avogadro::View& view, float width,
                            float height, float sx, float sy);
} // namespace overlay
