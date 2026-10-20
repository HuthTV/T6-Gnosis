#pragma once
#include "despawn.hpp"
#include "despawn_schedule.hpp"
#include <imgui.h>
namespace despawn::ui {
struct View {
    Counts counts;
    bool living_valid=false,observer_valid=false,distance_tracker=false;
    // The map runs no distance tracking (survival modes and Nuketown never start
    // zombie_tracking_init, so level.zombie_tracking_wait stays undefined and the
    // helper publishes interval -1). Nothing about despawns applies there.
    bool unsupported=false;
    std::shared_ptr<const Snapshot> observer;
    Events observed;
    schedule::Sample scheduled;
    schedule::PassResults results;
    std::uint64_t now=0;
    std::shared_ptr<const nav::Graph> map;  // map the view was captured for (HUD placement)
};
// Menu options, saved with the overlay settings. Master switch (user request
// 2026-10-13): off hides the HUD and keeps wallhack arrows plain red; on colours them
// by cleanup category.
struct Settings {bool enabled=false,show_hud=true,show_phase=true,outside_nearby_only=false;};
extern Settings settings;
View capture(const std::shared_ptr<const nav::Graph>& map);
// Despawn options for the Zombies section; wallhack enables the arrow-only options.
void menu(const View& view,bool wallhack);
void hud(const View& view,float width,float height);
ImU32 arrow_color(const View& view,int entity);
bool arrow_eye(const View& view,int entity);
bool arrow_visible(const View& view,int entity);
}
