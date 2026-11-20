// The F8 menu: one section per feature, map-specific sections only on their map.
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include "overlay_shared.hpp"
#include "version.hpp"

namespace overlay
{
    namespace
    {
        // Hover help for the item just drawn.
        void tip(const char* text)
        {
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30);
                ImGui::TextUnformatted(text);
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }

        // Full-width radius slider. The label lives in the format, with the derived height.
        void radius_slider(const char* id, float* value, float lo, float hi, float share = vertical_share)
        {
            char format[64]{};
            std::snprintf(format, sizeof(format), "Radius %%.0f  (+/- %.0f height)", *value * share);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat(id, value, lo, hi, format, ImGuiSliderFlags_AlwaysClamp);
        }

        // Section header. status, when given, is drawn right-aligned on the bar in the
        // disabled-text colour.
        bool section(const char* name, bool open = true, const char* status = nullptr)
        {
            const bool shown = ImGui::CollapsingHeader(name, open ? ImGuiTreeNodeFlags_DefaultOpen : 0);
            if (status && *status)
            {
                const auto lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
                const auto size = ImGui::CalcTextSize(status);
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(hi.x - size.x - ImGui::GetStyle().FramePadding.x * 2, lo.y + (hi.y - lo.y - size.y) * 0.5f),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled), status);
            }
            return shown;
        }
    } // namespace

    // Menu layout (user request 2026-10-13): master switch, map-specific aids, Movement
    // HUD, Zombies (wallhack + despawn), Spawns, Collision, Triggers, then NAV and quick
    // controls. Long explanations are tooltips. Finer options in the sections themselves.
    void menu(const FrameInfo& frame)
    {
        const auto display = ImGui::GetIO().DisplaySize;
        const ImVec2 size(std::max(300.0f, std::min(560.0f, display.x - 40)), std::max(280.0f, std::min(900.0f, display.y - 80)));
        // Position and size persist in T6Gnosis.cfg (menu_x/y/w/h). Reset restores the defaults.
        const bool reset = take_window_reset();
        const ImGuiCond placement = reset ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
        const bool saved_place = !reset && settings.menu_w > 0 && settings.menu_h > 0;
        ImGui::SetNextWindowSize(saved_place ? ImVec2(settings.menu_w, settings.menu_h) : size, placement);
        ImGui::SetNextWindowPos(saved_place ? ImVec2(settings.menu_x, settings.menu_y)
                                            : ImVec2(50, std::max(25.0f, std::min(120.0f, display.y - size.y - 25))),
                                placement);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 16));
        if (ImGui::Begin("T6 Gnosis " GNOSIS_VERSION "###T6 Gnosis", &menu_open, ImGuiWindowFlags_NoCollapse))
        {
            const auto place = ImGui::GetWindowPos(), extent = ImGui::GetWindowSize();
            settings.menu_x = place.x;
            settings.menu_y = place.y;
            settings.menu_w = extent.x;
            settings.menu_h = extent.y;
            // Master switch: everything below draws only while it is on.
            ImGui::PushStyleColor(ImGuiCol_Button, settings.visible ? ImVec4(0.16f, 0.55f, 0.30f, 1) : ImVec4(0.35f, 0.30f, 0.40f, 1));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  settings.visible ? ImVec4(0.22f, 0.66f, 0.38f, 1) : ImVec4(0.45f, 0.38f, 0.52f, 1));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                  settings.visible ? ImVec4(0.27f, 0.75f, 0.44f, 1) : ImVec4(0.52f, 0.44f, 0.60f, 1));
            const float reset_width = ImGui::CalcTextSize("Reset").x + ImGui::GetStyle().FramePadding.x * 4;
            if (ImGui::Button(settings.visible ? "OVERLAY ON" : "OVERLAY OFF",
                              ImVec2(-(reset_width + ImGui::GetStyle().ItemSpacing.x), 34)))
                settings.visible = !settings.visible;
            ImGui::PopStyleColor(3);
            tip("Master switch for everything T6 Gnosis draws. The settings below are kept while it is off.");
            ImGui::SameLine();
            if (ImGui::Button("Reset", ImVec2(reset_width, 34))) ImGui::OpenPopup("Reset settings");
            tip("Restore every setting, quick-control key and the menu's size and position to the defaults.");
            if (ImGui::BeginPopup("Reset settings"))
            {
                ImGui::TextUnformatted("Reset all settings to defaults?");
                if (ImGui::Button("Reset", ImVec2(90, 0)))
                {
                    reset_settings();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(90, 0))) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            ImGui::Spacing();
            ImGui::BeginDisabled(!settings.visible);

            // Avogadro section first on TranZit (user request 2026-10-30).
            const bool transit = frame.graph && frame.graph->map == "maps/mp/zm_transit.d3dbsp";
            if (transit && section("Avogadro (TranZit)"))
            {
                ImGui::Checkbox("Avogadro aid", &settings.avogadro_aid);
                tip("His state, hits and shield, and what he does next (phase dash cooldown, the three bolt conditions, when he leaves), over his head when he is near, else at the top of the screen. In the storm cloud the spots he can land on are marked. The bolt wedge at his feet: he can only fire with you 120-600 units away inside it. Classic TranZit only; he waits in the power station chamber until the power is on.");
                ImGui::SameLine();
                ImGui::Checkbox("Spawn locations", &settings.avogadro_spawns);
                tip("Every avogadro_location, his own landing spots (not zombie spawns), through walls. Bright with a beam: where he can land from the storm right now.");
                if (settings.avogadro_aid)
                {
                    ImGui::Checkbox("Dashes", &settings.avogadro_dashes);
                    tip("The 12 phase dashes his next roll picks from (forward, left, right, back; 3 lengths each), where the game's own check puts them from his current facing: yellow fits, red cross does not. Bright once the 2 s cooldown is over; then each 0.1 s he picks one at random and dashes only if it fits. He turns toward you as a dash starts, so the real dash can swing toward you.");
                    ImGui::SameLine();
                    ImGui::Checkbox("Route", &settings.avogadro_route);
                    tip("The route the engine planned for him over the NAV graph: the same pathing as ordinary zombies. A hollow ring marks where a long route is cut; the rest is planned as he walks.");
                    ImGui::SameLine();
                    ImGui::Checkbox("Keep text at top", &settings.avogadro_locked);
                    tip("Always show his lines at the top of the screen, never over his head.");
                    if (!settings.avogadro_locked)
                    {
                        ImGui::SetNextItemWidth(-1);
                        ImGui::SliderFloat("##avogadro_radius", &settings.avogadro_range, 200, 8000, "Over his head within %.0f units",
                                           ImGuiSliderFlags_AlwaysClamp);
                    }
                }
            }

            const bool origins = frame.graph && frame.graph->map == "maps/mp/zm_tomb.d3dbsp";
            if (origins && section("Origins puzzle aids"))
            {
                ImGui::Checkbox("Wind puzzle aid", &settings.wind_aid);
                tip("Wind puzzle 2 only. Boxes: green solved, red unsolved, yellow ignoring hits; arrow toward the destination. Ring: where the held wind staff's shot would land; green solves, red fails, yellow uncertain, gray miss.");
                if (settings.wind_aid)
                {
                    ImGui::SameLine();
                    ImGui::Checkbox("Details##wind", &settings.wind_details);
                    tip("Verdict and live numbers.");
                }
                ImGui::Checkbox("Fire puzzle aid", &settings.fire_aid);
                tip("Rings: where the fire staff's main shot and two side shots explode, whenever the fire staff is held (also upgraded, uncharged). During fire puzzle 2 only, spheres: the 64-unit torch hitboxes; green correct lit, orange correct unlit, red wrong lit, gray wrong unlit, and the rings turn green when a shot lights a correct torch, red a wrong torch, yellow ignored, gray nothing.");
                if (settings.fire_aid)
                {
                    ImGui::SameLine();
                    ImGui::Checkbox("Details##fire", &settings.fire_details);
                    tip("Summary line and live numbers.");
                }
                ImGui::Checkbox("Robot footstep clue radius", &settings.robot_steps);
                tip("For the walking robot with the hatch (the wind staff robot): an orb on every big step still ahead on its route, the 1500-unit radius around the foot inside which that step sends Samantha's wind staff clue, when you stand inside it 0.2 s after the impact, alive and outside every robot head. The test is 3D: the bright ring is where the sphere meets your height, the faint dome is its surface just above. Green: you are inside. The next step is brighter. Each impact sends out a red shockwave. Steps whose sphere holds no walkable ground are left out, and the intro walk at game start shows none (no clue can play then).Dots: where you stood at each check, green when the game sent the clue, red when not. Gold diamond: the hatch foot, filled once its hatch is open.");
                if (settings.robot_steps)
                {
                    ImGui::SameLine();
                    ImGui::Checkbox("Details##robot", &settings.robot_details);
                    tip("Distance to the nearest foot, the route's step count and how far the last step landed from its recorded spot, each step's distance and whether the game sent the clue, and the clue's 10 s wait before it checks for zombie blood (the helper's copy of that wait).");
                }
            }
            const bool mob_map = frame.graph && frame.graph->map == "maps/mp/zm_prison.d3dbsp";
            if (mob_map && section("Mob of the Dead"))
            {
                ImGui::Checkbox("Blue skull aid", &settings.mob_skulls);
                tip("Blundergat quest skulls (after the Hell's Retriever): each skull's 100-unit catch sphere, green when the predicted throw catches it and cyan while a lightning-hands hit reveals it. The predicted tomahawk arc ends at the game's single check point: the first impact, or 0.55 s after the throw. The last real throw stays drawn for a few seconds. Prediction fitted from standing throws; running throws are approximate.");
                ImGui::SameLine();
                ImGui::Checkbox("Shock boxes (Afterlife)", &settings.mob_shock_boxes);
                tip("In Afterlife: each shock box's bounds and a ring at your height showing where you are within its 256-unit activation range (green when in range).");
                ImGui::Checkbox("Afterlife spawn spots", &settings.mob_afterlife_spawns);
                tip("Every spot an Afterlife ghost can spawn at. Green: where you would spawn if you went down now (zone rule). Gold: a spot the game locked to you; once a random spot is picked in a group it is reused for the rest of the game. Orange: the free spots one of which would be picked at random (and then locked). Blue: other spots.");
                if (settings.mob_afterlife_spawns)
                {
                    ImGui::SetNextItemWidth(220);
                    ImGui::SliderFloat("##mob_afterlife_radius", &settings.mob_afterlife_range, 300, 8000, "Spots within %.0f units",
                                       ImGuiSliderFlags_AlwaysClamp);
                    tip("Only spots this close to you are drawn; the ones you would spawn at are always drawn.");
                    ImGui::SameLine();
                    ImGui::Checkbox("Only where I would spawn", &settings.mob_afterlife_only);
                    tip("Hide every spot except the one (or the random candidates) you would spawn at right now.");
                }
            }
            if (section("Grenade throws"))
            {
                ImGui::Checkbox("Throw paths", &settings.grenade_arcs);
                tip("Frag, EMP and G-strike: the predicted path whenever you carry one, with the bounces and where it rests or bursts in the air (red ring). While one is held back, that one, with the cooking counted. EMP: the zombie-stun radius (blue) and the perk and power radius (yellow). G-strike: where the five shells land with their 200-unit kill radius, gray when it would be a dud outside an active zone (approximate at zone edges). The last real throw stays drawn in yellow for a few seconds, with the prediction made just before it in gray. The throw model is not measured for these grenades yet: trust the yellow path, not the prediction.");
                ImGui::SameLine();
                if (ImGui::RadioButton("Lethal first", !settings.grenade_tactical_first)) settings.grenade_tactical_first = false;
                ImGui::SameLine();
                if (ImGui::RadioButton("Tactical first", settings.grenade_tactical_first)) settings.grenade_tactical_first = true;
                tip("Which path shows when you carry both and hold neither back: the lethal (frag) or the tactical (EMP, G-strike). The other one shows when the preferred one is out of ammo.");
            }
            if (section("Movement HUD"))
            {
                ImGui::Checkbox("Stamina bar", &settings.stamina_bar);
                ImGui::SameLine();
                ImGui::Checkbox("Speedometer", &settings.speedometer);
                ImGui::SameLine();
                ImGui::Checkbox("Jump stats", &settings.jump_stats);
            }

            if (section("Zombies"))
            {
                ImGui::Checkbox("Wallhack", &settings.wallhack);
                tip("Arrows above living zombies, through walls; diamonds for special enemies. With the despawn tracker on, arrows are coloured by cleanup category.");
                despawn::ui::menu(frame.despawn_view, settings.wallhack);
            }

            // Site counts ride on the Spawns bar while spawn points are drawn.
            char sites[96]{};
            if (settings.spawn_points && frame.spawn_valid)
            {
                const auto& s = *frame.spawn_snapshot;
                std::snprintf(sites, sizeof(sites), "Sites: %u total / %u active / %u drawn", static_cast<unsigned>(s.sites.size()),
                              s.active_count, frame.spawn_count);
            }
            if (section("Spawns", true, sites))
            {
                ImGui::Checkbox("Spawn points", &settings.spawn_points);
                ImGui::SameLine();
                ImGui::Checkbox("Labels##spawns", &settings.spawn_labels);
                ImGui::Checkbox("Special spawn locations", &settings.special_spawns);
                ImGui::SameLine();
                ImGui::Checkbox("Only active", &settings.active_spawns_only);
                radius_slider("##spawn_radius", &settings.spawn_range, 400, 8000);
            }

            if (section("Player collision"))
            {
                ImGui::Checkbox("World collision", &settings.world_collision);
                tip("Solid faces, darker edges; only the nearest collision surface shows. World brushes with solid or player-clip contents, plus collision triangles (two-sided, coloured by their stored facing). Moving brush-model entities (doors, elevators, bus clips) and model collision (props, the bus body, the Origins tank, a placed shield) are not drawn. The player collides as a rounded body about 15 units in radius, so it stops short of brush corners. Drawn under the game HUD.");
                ImGui::SameLine();
                ImGui::Checkbox("Zombie hitboxes", &settings.zombie_boxes);
                tip("Each living zombie's entity bounds, the box the game uses for its collision. Works with or without world collision.");
                ImGui::SameLine();
                ImGui::Checkbox("bbox", &settings.player_bounds);
                tip("The player entity's axis-aligned bounds (30 x 30 x 70). Movement collides as a round body of radius 15, so the corners reach further than you do.");
                if (settings.world_collision)
                {
                    // Colour key for the collision surfaces, all always drawn.
                    ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.47f, 1), "Floors");
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.3f, 0.78f, 1, 1), "Walls");
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1, 0.63f, 0.24f, 1), "Roofs");
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1, 0.31f, 1, 1), "Invisible");
                    tip("Clip-only brushes: they block the player but have no visible surface.");
                    radius_slider("##collision_radius", &settings.collision_range, 100, 3000, 1.0f);
                    ImGui::SetNextItemWidth(-1);
                    ImGui::SliderFloat("##collision_opacity", &settings.collision_opacity, 0.2f, 1.0f, "Opacity %.2f",
                                       ImGuiSliderFlags_AlwaysClamp);
                    ImGui::Checkbox("Models on top", &settings.collision_in_frame);
                    tip("Props, corpses, zombies and the weapon show over the collision when they are in front of it; where the collision is clearly in front of a model (a clip wall, a model inside a clip), the model shows through a light tint. Each model draw is replayed into a mask inside the game's frame. Off: boxes decide what covers the collision.");
                }
            }

            if (section("Triggers"))
            {
                ImGui::Checkbox("Trigger geometry", &settings.trigger_geometry);
                tip("Native boxes, vertical cylinders and decoded brush hulls; red shapes draw through walls. A shape is drawn whole when any part overlaps the radius. Unsupported geometry is skipped.");
                ImGui::SameLine();
                ImGui::Checkbox("Labels##triggers", &settings.trigger_labels);
                ImGui::SameLine();
                ImGui::Checkbox("Playable area", &settings.playable_area);
                tip("Green: enabled player volumes; bright green: life brushes; red: kill brushes; gray: other enabled overlays. Life brushes override kill regions. Decoded base monitor volumes only; zone state updates once per second.");
                radius_slider("##trigger_radius", &settings.trigger_range, 400, 8000);
            }

            if (section("NAV", false))
            {
                ImGui::Checkbox("Nodes", &settings.nav_nodes);
                ImGui::SameLine();
                ImGui::Checkbox("Links", &settings.nav_links);
                ImGui::SameLine();
                ImGui::Checkbox("IDs", &settings.nav_ids);
                ImGui::SameLine();
                ImGui::Checkbox("Directions", &settings.nav_directions);
                radius_slider("##nav_radius", &settings.nav_range, 400, 8000);
                ImGui::TextDisabled("Graph: %u nodes | %u links", frame.graph ? frame.graph->count : 0,
                                    frame.graph ? frame.graph->directed_links : 0);
            }
            ImGui::EndDisabled();

            if (ImGui::CollapsingHeader("Quick controls"))
            {
                ImGui::TextWrapped(
                    "One key per top-level option: click a button, then press a key. Escape cancels, Backspace clears. Keys are saved with the settings.");
                for (std::size_t i = 0; i < quick.size(); ++i)
                {
                    ImGui::PushID(static_cast<int>(i));
                    char name[64]{};
                    key_label(quick[i].key, name, sizeof(name));
                    if (ImGui::Button(key_capture() == static_cast<int>(i) ? "Press key..." : name, ImVec2(110, 0)))
                        start_key_capture(static_cast<int>(i));
                    ImGui::SameLine();
                    ImGui::TextUnformatted(quick[i].label);
                    ImGui::PopID();
                }
            }
            ImGui::Spacing();
            if (ImGui::Button("Close menu", ImVec2(-1, 30))) menu_open = false;
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }
} // namespace overlay
