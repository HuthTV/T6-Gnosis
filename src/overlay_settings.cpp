// Overlay options, their file (T6Gnosis.cfg) and keyboard input: the menu keys and the
// quick-control keys bound in the menu.
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include "overlay_shared.hpp"

namespace overlay
{
    Settings settings;

    std::array<QuickBinding, 22> quick{{
        {"Overlay master switch", "key_overlay", &settings.visible},
        {"Stamina bar", "key_stamina_bar", &settings.stamina_bar},
        {"Speedometer", "key_speedometer", &settings.speedometer},
        {"Jump stats", "key_jump_stats", &settings.jump_stats},
        {"Wallhack", "key_wallhack", &settings.wallhack},
        {"Despawn tracker", "key_despawn_tracker", &despawn::ui::settings.enabled},
        {"Spawn points", "key_spawn_points", &settings.spawn_points},
        {"World collision", "key_world_collision", &settings.world_collision},
        {"Zombie hitboxes", "key_zombie_hitboxes", &settings.zombie_boxes},
        {"bbox", "key_bbox", &settings.player_bounds},
        {"Trigger geometry", "key_triggers", &settings.trigger_geometry},
        {"Playable area", "key_playable_area", &settings.playable_area},
        {"NAV nodes", "key_nav_nodes", &settings.nav_nodes},
        {"NAV links", "key_nav_links", &settings.nav_links},
        {"Origins wind aid", "key_wind_aid", &settings.wind_aid},
        {"Origins fire aid", "key_fire_aid", &settings.fire_aid},
        {"Origins robot steps", "key_robot_steps", &settings.robot_steps},
        {"Mob blue skull aid", "key_mob_skulls", &settings.mob_skulls},
        {"Mob shock boxes", "key_mob_shock_boxes", &settings.mob_shock_boxes},
        {"Mob Afterlife spawns", "key_mob_afterlife_spawns", &settings.mob_afterlife_spawns},
        {"Grenade throws", "key_grenade_arcs", &settings.grenade_arcs},
        {"TranZit Avogadro aid", "key_avogadro_aid", &settings.avogadro_aid},
    }};

    namespace
    {
        // Saved settings (user request 2026-10-13): T6Gnosis.cfg next to the DLL, one
        // key=value per line. The keys are the file format, so keep their spelling when
        // renaming variables. Unknown keys are ignored. Missing keys keep their defaults.
        struct BoolSetting
        {
            const char* key;
            bool* value;
            bool fallback = false;
        };

        struct FloatSetting
        {
            const char* key;
            float* value;
            float lo, hi;
            float fallback = 0;
        };

        BoolSetting bool_settings[] = {
            {"overlay", &settings.visible},
            {"wallhack", &settings.wallhack},
            {"despawn_tracker", &despawn::ui::settings.enabled},
            {"despawn_counts", &despawn::ui::settings.show_hud},
            {"despawn_timer", &despawn::ui::settings.show_phase},
            {"despawn_out_of_range_only", &despawn::ui::settings.outside_nearby_only},
            {"stamina_bar", &settings.stamina_bar},
            {"speedometer", &settings.speedometer},
            {"jump_stats", &settings.jump_stats},
            {"wind_aid", &settings.wind_aid},
            {"wind_details", &settings.wind_details},
            {"fire_aid", &settings.fire_aid},
            {"fire_details", &settings.fire_details},
            {"robot_steps", &settings.robot_steps},
            {"robot_details", &settings.robot_details},
            {"mob_skulls", &settings.mob_skulls},
            {"mob_shock_boxes", &settings.mob_shock_boxes},
            {"mob_afterlife_spawns", &settings.mob_afterlife_spawns},
            {"mob_afterlife_only", &settings.mob_afterlife_only},
            {"avogadro_aid", &settings.avogadro_aid},
            {"avogadro_dashes", &settings.avogadro_dashes},
            {"avogadro_route", &settings.avogadro_route},
            {"avogadro_locked", &settings.avogadro_locked},
            {"avogadro_spawns", &settings.avogadro_spawns},
            {"grenade_arcs", &settings.grenade_arcs},
            {"grenade_tactical_first", &settings.grenade_tactical_first},
            {"spawn_points", &settings.spawn_points},
            {"spawn_labels", &settings.spawn_labels},
            {"special_spawns", &settings.special_spawns},
            {"active_spawns_only", &settings.active_spawns_only},
            {"world_collision", &settings.world_collision},
            {"collision_in_frame", &settings.collision_in_frame},
            {"zombie_collision_boxes", &settings.zombie_boxes},
            {"player_bounds", &settings.player_bounds},
            {"triggers", &settings.trigger_geometry},
            {"trigger_labels", &settings.trigger_labels},
            {"playable_area", &settings.playable_area},
            {"nav_nodes", &settings.nav_nodes},
            {"nav_links", &settings.nav_links},
            {"nav_ids", &settings.nav_ids},
            {"nav_directions", &settings.nav_directions},
        };

        FloatSetting float_settings[] = {
            {"spawn_radius", &settings.spawn_range, 400, 8000},
            {"avogadro_radius", &settings.avogadro_range, 200, 8000},
            {"mob_afterlife_radius", &settings.mob_afterlife_range, 300, 8000},
            {"collision_radius", &settings.collision_range, 100, 3000},
            {"collision_opacity", &settings.collision_opacity, 0.2f, 1},
            {"trigger_radius", &settings.trigger_range, 400, 8000},
            {"nav_radius", &settings.nav_range, 400, 8000},
            {"menu_x", &settings.menu_x, -1, 16384},
            {"menu_y", &settings.menu_y, -1, 16384},
            {"menu_w", &settings.menu_w, -1, 16384},
            {"menu_h", &settings.menu_h, -1, 16384},
        };

        bool loaded = false, window_reset = false;
        std::string saved_text;
        std::array<bool, 256> down{};
        int capture = -1;

        // Path of a file beside the DLL. Empty when the module path is unavailable.
        std::string beside_dll(const char* name)
        {
            HMODULE module = nullptr;
            char path[MAX_PATH]{};
            if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    reinterpret_cast<LPCSTR>(&beside_dll), &module) ||
                !GetModuleFileNameA(module, path, MAX_PATH))
                return {};
            char* slash = std::strrchr(path, '\\');
            if (!slash) return {};
            slash[1] = 0;
            return std::string(path) + name;
        }

        std::string settings_text()
        {
            std::string text = "# T6 Gnosis settings, saved by the F8 menu. Delete this file or press Reset to restore defaults.\n";
            char line[96]{};
            for (const auto& s : bool_settings)
            {
                std::snprintf(line, sizeof(line), "%s=%d\n", s.key, *s.value ? 1 : 0);
                text += line;
            }
            for (const auto& s : float_settings)
            {
                std::snprintf(line, sizeof(line), "%s=%g\n", s.key, *s.value);
                text += line;
            }
            for (const auto& binding : quick)
            {
                std::snprintf(line, sizeof(line), "%s=%d\n", binding.setting, binding.key);
                text += line;
            }
            return text;
        }
    } // namespace

    bool settings_loaded() { return loaded; }

    void load_settings()
    {
        loaded = true;
        for (auto& s : bool_settings)
            s.fallback = *s.value;
        for (auto& s : float_settings)
            s.fallback = *s.value;
        const auto path = beside_dll("T6Gnosis.cfg");
        FILE* file = nullptr;
        if (!path.empty() && !fopen_s(&file, path.c_str(), "rb") && file)
        {
            char line[160]{};
            while (std::fgets(line, sizeof(line), file))
            {
                char* equals = std::strchr(line, '=');
                if (line[0] == '#' || !equals) continue;
                *equals = 0;
                const char* value = equals + 1;
                for (auto& s : bool_settings)
                    if (!std::strcmp(line, s.key)) *s.value = std::atoi(value) != 0;
                for (auto& s : float_settings)
                    if (!std::strcmp(line, s.key))
                    {
                        const float parsed = std::strtof(value, nullptr);
                        if (std::isfinite(parsed)) *s.value = std::clamp(parsed, s.lo, s.hi);
                    }
                for (auto& binding : quick)
                    if (!std::strcmp(line, binding.setting))
                    {
                        const int key = std::atoi(value);
                        binding.key = key > 0 && key < 256 && key != VK_F8 && key != VK_F9 ? key : 0;
                    }
            }
            std::fclose(file);
        }
        saved_text = settings_text();
    }

    // Writes the file only when a setting changed. Replaces it whole so a crash mid-write
    // cannot leave a truncated file.
    void save_settings()
    {
        if (!loaded) return;
        auto text = settings_text();
        if (text == saved_text) return;
        const auto path = beside_dll("T6Gnosis.cfg");
        if (path.empty()) return;
        const auto temporary = path + ".tmp";
        FILE* file = nullptr;
        if (fopen_s(&file, temporary.c_str(), "wb") || !file) return;
        const bool written = std::fwrite(text.data(), 1, text.size(), file) == text.size();
        if (std::fclose(file) != 0 || !written || !MoveFileExA(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
        {
            DeleteFileA(temporary.c_str());
            return;
        }
        saved_text = std::move(text);
    }

    void reset_settings()
    {
        for (auto& s : bool_settings)
            *s.value = s.fallback;
        for (auto& s : float_settings)
            *s.value = s.fallback;
        for (auto& binding : quick)
            binding.key = 0;
        capture = -1;
        window_reset = true;
    }

    bool take_window_reset() { return std::exchange(window_reset, false); }

    bool key_pressed(int key)
    {
        const bool now = (GetAsyncKeyState(key) & 0x8000) != 0;
        const bool edge = now && !down[static_cast<std::size_t>(key)];
        down[static_cast<std::size_t>(key)] = now;
        return edge;
    }

    void forget_keys() { down.fill(false); }

    void handle_quick_keys(bool menu_is_open)
    {
        if (capture >= 0 && menu_is_open)
        {
            // The next key pressed (modifiers and the menu keys excluded) becomes the binding.
            // Escape cancels, Backspace clears. A key moves from any other binding to this one.
            for (int key = VK_BACK; key < 256; ++key)
            {
                if (key == VK_F8 || key == VK_F9 || key == VK_SHIFT || key == VK_CONTROL || key == VK_MENU || key == VK_LSHIFT ||
                    key == VK_RSHIFT || key == VK_LCONTROL || key == VK_RCONTROL || key == VK_LMENU || key == VK_RMENU)
                    continue;
                if (!key_pressed(key)) continue;
                if (key != VK_ESCAPE)
                {
                    const int assigned = key == VK_BACK ? 0 : key;
                    if (assigned)
                        for (auto& binding : quick)
                            if (binding.key == assigned) binding.key = 0;
                    quick[static_cast<std::size_t>(capture)].key = assigned;
                }
                capture = -1;
                break;
            }
        }
        else if (!menu_is_open)
        {
            for (auto& binding : quick)
                if (binding.key && key_pressed(binding.key)) *binding.value = !*binding.value;
        }
    }

    void start_key_capture(int index)
    {
        capture = index;
        // Keys already held must not count as the new binding.
        for (int key = 0; key < 256; ++key)
            down[static_cast<std::size_t>(key)] = (GetAsyncKeyState(key) & 0x8000) != 0;
    }

    int key_capture() { return capture; }

    void cancel_key_capture() { capture = -1; }

    void key_label(int key, char* out, int size)
    {
        if (!key)
        {
            std::snprintf(out, size, " ");
            return;
        }
        const auto scan = MapVirtualKeyW(static_cast<UINT>(key), MAPVK_VK_TO_VSC_EX);
        if (!GetKeyNameTextA(static_cast<LONG>((scan & 255) << 16) | ((scan & 0xFF00) ? (1 << 24) : 0), out, size))
            std::snprintf(out, size, "Key %d", key);
    }
} // namespace overlay
