// Plugin entry point: registers the GSC builtins, installs the hooks and schedules the
// readers. Every reader checks its engine bindings (fingerprints) first and stays off on
// an unsupported game build. The console gets one ready line, plus one line for each part
// that cannot start.
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <plutonium_sdk.hpp>
#include "navigation.hpp"
#include "overlay.hpp"
#include "zombies.hpp"
#include "triggers.hpp"
#include "spawns.hpp"
#include "movement.hpp"
#include "despawn_bridge.hpp"
#include "despawn.hpp"
#include "despawn_schedule.hpp"
#include "staff_aid.hpp"
#include "mob_aid.hpp"
#include "grenade_aid.hpp"
#include "robot_aid.hpp"
#include "avogadro_aid.hpp"
#include "world_collision.hpp"
#include "helper_deploy.hpp"
#include "version.hpp"

namespace
{
    using scheduler = plutonium::sdk::interfaces::scheduler;

    // The helper's "plugin present" test (builtinfunctionexists). It never runs anything.
    void PLUTONIUM_CALLBACK plugin_loaded() {}

    // Drops every snapshot (game shutdown and plugin unload).
    void clear_all(int = 0)
    {
        nav::clear_graph();
        zombies::clear();
        spawns::clear();
        despawn::clear();
        triggers::clear();
        staff::clear();
        mob::clear();
        grenade::clear();
        robot::clear();
        avogadro::clear();
        collision::clear();
    }

    // Each frame: living zombies and the despawn scheduler.
    void refresh_zombies()
    {
        try
        {
            zombies::update();
            despawn::schedule::update();
        }
        catch (...)
        {
            zombies::clear();
            despawn::schedule::clear();
        }
    }

    // Twice a second: spawn sites, trigger volumes and world collision.
    scheduler::evaluation refresh_world()
    {
        try
        {
            spawns::update();
        }
        catch (...)
        {
            spawns::clear();
        }
        try
        {
            triggers::update();
        }
        catch (...)
        {
            triggers::clear();
        }
        try
        {
            collision::update();
        }
        catch (...)
        {
            collision::clear();
        }
        return scheduler::reschedule;
    }

    // Once a second: the NAV graph, which also tells every drawing which map is loaded.
    scheduler::evaluation refresh_nav()
    {
        try
        {
            nav::refresh_graph();
        }
        catch (...)
        {
            nav::clear_graph();
        }
        return scheduler::reschedule;
    }

    class Plugin final : public plutonium::sdk::plugin
    {
        bool active = false;

        const char* plugin_name() override { return "T6 Gnosis (solo LAN)"; }

        bool is_game_supported(plutonium::sdk::game game) override { return game == plutonium::sdk::game::t6; }

        void on_startup(plutonium::sdk::iinterface* api, plutonium::sdk::game) override
        {
            auto unavailable = [&](bool ok, const char* text) {
                if (!ok) api->logging()->info(text);
            };
            // Registered first and unconditionally: the helper does nothing without it.
            api->gsc()->register_function("GnosisPluginLoaded", plugin_loaded);
            unavailable(helper_deploy::deploy() != helper_deploy::Result::failed,
                        "[T6 Gnosis] Could not write the GSC helper; helper features stay off.");

            // Builtins the helper calls (each module registers its own).
            unavailable(spawns::startup(api), "[T6 Gnosis] Spawn reader unavailable on this game build.");
            unavailable(staff::startup(api), "[T6 Gnosis] Origins aids unavailable on this game build.");
            unavailable(mob::startup(api), "[T6 Gnosis] Mob of the Dead aids unavailable on this game build.");
            unavailable(grenade::startup(api), "[T6 Gnosis] Grenade throw aid unavailable on this game build.");
            unavailable(robot::startup(api), "[T6 Gnosis] Origins robot footstep aid unavailable on this game build.");
            unavailable(avogadro::startup(api), "[T6 Gnosis] TranZit Avogadro aid unavailable on this game build.");
            unavailable(despawn::startup(api), "[T6 Gnosis] Despawn tracker unavailable on this game build.");

            // A script restart can reuse the same graph allocation: reset what the helper
            // published at script execution, as well as at game shutdown.
            api->callbacks()->on_scripts_execute(spawns::clear);
            api->callbacks()->on_scripts_execute(triggers::clear);
            api->callbacks()->on_scripts_execute(despawn::clear);
            api->callbacks()->on_scripts_execute(staff::clear);
            api->callbacks()->on_scripts_execute(mob::clear);
            api->callbacks()->on_scripts_execute(grenade::clear);
            api->callbacks()->on_scripts_execute(robot::clear);
            api->callbacks()->on_scripts_execute(avogadro::clear);
            api->callbacks()->on_scripts_execute(collision::clear);

            // Everything below needs the core engine bindings and the renderer hook.
            if (!nav::bindings_match())
            {
                api->logging()->info("[T6 Gnosis] Unsupported game build; disabled.");
                return;
            }
            active = overlay::startup();
            if (!active)
            {
                api->logging()->info("[T6 Gnosis] Could not hook the renderer; disabled.");
                return;
            }
            movement::startup(api);
            unavailable(zombies::startup(), "[T6 Gnosis] Zombie markers unavailable on this game build.");
            unavailable(triggers::startup(), "[T6 Gnosis] Trigger geometry unavailable on this game build.");
            api->callbacks()->on_game_shutdown(clear_all);
            api->scheduler()->every(refresh_nav, 1000, scheduler::thread::game);
            api->scheduler()->every(refresh_world, 500, scheduler::thread::game);
            api->scheduler()->on_frame(refresh_zombies, scheduler::thread::game);
            api->logging()->info("[T6 Gnosis] " GNOSIS_VERSION " ready. F8 opens the menu.");
        }

        void on_shutdown() override
        {
            movement::shutdown();
            if (active) overlay::shutdown();
            clear_all();
        }
    };
} // namespace

PLUTONIUM_API plutonium::sdk::plugin* on_initialize()
{
    static Plugin plugin;
    return &plugin;
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
