#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "mob_aid.hpp"
#include <atomic>
#include <mutex>
#include "gsc_args.hpp"

namespace mob
{
    namespace
    {
        std::mutex guard;
        std::atomic<bool> ready{false};
        View current;

        // Skulls and shock boxes are small props. Anything far larger is not one.
        bool box_bounds(nav::Vec3 lo, nav::Vec3 hi)
        {
            return lo.x <= hi.x && lo.y <= hi.y && lo.z <= hi.z && hi.x - lo.x <= 512 && hi.y - lo.y <= 512 && hi.z - lo.z <= 512;
        }

        void state_impl()
        {
            gsc_args::Arguments args;
            int afterlife = 0, tomahawk = 0;
            State state;
            if (!args.capture(3) || !args.integer(0, afterlife) || !args.integer(1, tomahawk) || !args.vector(2, state.player) ||
                !args.unchanged())
                return;
            if (afterlife < 0 || afterlife > 1 || tomahawk < 0 || tomahawk > 1) return;
            state.afterlife = afterlife == 1;
            state.tomahawk = tomahawk == 1;
            state.updated = GetTickCount64();
            std::lock_guard lock(guard);
            current.state = state;
        }

        void skull_impl()
        {
            gsc_args::Arguments args;
            int index = 0;
            Skull skull;
            if (!args.capture(5) || !args.integer(0, index) || !args.vector(1, skull.origin) || !args.vector(2, skull.mins) ||
                !args.vector(3, skull.maxs) || !args.number(4, skull.shown_ms) || !args.unchanged())
                return;
            if (index < 0 || index >= static_cast<int>(current.skulls.size()) || !box_bounds(skull.mins, skull.maxs) ||
                skull.shown_ms > 2000)
                return;
            skull.updated = GetTickCount64();
            std::lock_guard lock(guard);
            current.skulls[static_cast<std::size_t>(index)] = skull;
        }

        void aim_impl()
        {
            gsc_args::Arguments args;
            int skull = 0, impact = 0;
            Aim aim;
            if (!args.capture(6) || !args.vector(0, aim.start) || !args.vector(1, aim.launch) || !args.number(2, aim.stop) ||
                !args.vector(3, aim.check) || !args.integer(4, skull) || !args.integer(5, impact) || !args.unchanged())
                return;
            if (aim.stop < 0 || aim.stop > 1 || skull < -1 || skull >= static_cast<int>(current.skulls.size()) || impact < 0 || impact > 1)
                return;
            aim.skull = skull;
            aim.impact = impact == 1;
            aim.updated = GetTickCount64();
            std::lock_guard lock(guard);
            current.aim = aim;
        }

        void flight_impl()
        {
            gsc_args::Arguments args;
            int id = 0, check = 0, skull = 0;
            nav::Vec3 point{};
            if (!args.capture(4) || !args.integer(0, id) || !args.vector(1, point) || !args.integer(2, check) || !args.integer(3, skull) ||
                !args.unchanged())
                return;
            if (id <= 0 || check < 0 || check > 1 || skull < -1 || skull >= static_cast<int>(current.skulls.size())) return;
            std::lock_guard lock(guard);
            auto& f = current.flight;
            if (f.id != id) f = Flight{id};
            if (check)
            {
                f.done = true;
                f.check = point;
                f.skull = skull;
            }
            if (f.count < f.points.size()) f.points[f.count++] = point;
            f.updated = GetTickCount64();
        }

        void box_impl()
        {
            gsc_args::Arguments args;
            int index = 0, planar = 0;
            Box box;
            if (!args.capture(5) || !args.integer(0, index) || !args.vector(1, box.origin) || !args.vector(2, box.mins) ||
                !args.vector(3, box.maxs) || !args.integer(4, planar) || !args.unchanged())
                return;
            if (index < 0 || index >= static_cast<int>(current.boxes.size()) || !box_bounds(box.mins, box.maxs) || planar < 0 || planar > 1)
                return;
            box.planar = planar == 1;
            box.updated = GetTickCount64();
            std::lock_guard lock(guard);
            current.boxes[static_cast<std::size_t>(index)] = box;
        }

        void spawn_impl()
        {
            gsc_args::Arguments args;
            int index = 0, group = 0, state = 0;
            Spawn spawn;
            if (!args.capture(4) || !args.integer(0, index) || !args.vector(1, spawn.origin) || !args.integer(2, group) ||
                !args.integer(3, state) || !args.unchanged())
                return;
            if (index < 0 || index >= static_cast<int>(current.spawns.size()) || group < 0 || group > 255 || state < 0 || state > 7) return;
            spawn.group = group;
            spawn.state = static_cast<unsigned>(state);
            spawn.updated = GetTickCount64();
            std::lock_guard lock(guard);
            current.spawns[static_cast<std::size_t>(index)] = spawn;
        }

        void spawn_next_impl()
        {
            gsc_args::Arguments args;
            int kind = 0, index = 0, group = 0;
            if (!args.capture(3) || !args.integer(0, kind) || !args.integer(1, index) || !args.integer(2, group) || !args.unchanged())
                return;
            if (kind < 0 || kind > 3 || index < -1 || index >= static_cast<int>(current.spawns.size()) || group < -1 || group > 255) return;
            SpawnNext next;
            next.kind = kind;
            next.index = index;
            next.group = group;
            next.updated = GetTickCount64();
            std::lock_guard lock(guard);
            current.next = next;
        }

        template<void (*Callback)()> void invoke() noexcept
        {
            if (!ready.load()) return;
            try
            {
                Callback();
            }
            catch (...)
            {
                clear();
            }
        }

        void PLUTONIUM_CALLBACK state_callback() { invoke<state_impl>(); }

        void PLUTONIUM_CALLBACK skull_callback() { invoke<skull_impl>(); }

        void PLUTONIUM_CALLBACK aim_callback() { invoke<aim_impl>(); }

        void PLUTONIUM_CALLBACK flight_callback() { invoke<flight_impl>(); }

        void PLUTONIUM_CALLBACK box_callback() { invoke<box_impl>(); }

        void PLUTONIUM_CALLBACK spawn_callback() { invoke<spawn_impl>(); }

        void PLUTONIUM_CALLBACK spawn_next_callback() { invoke<spawn_next_impl>(); }
    } // namespace

    bool startup(plutonium::sdk::iinterface* api)
    {
        ready = false;
        clear();
        if (!api) return false;
        // Registered before the check, as in staff_aid. Mismatched builds ignore the calls.
        api->gsc()->register_function("GnosisMobState", state_callback);
        api->gsc()->register_function("GnosisMobSkull", skull_callback);
        api->gsc()->register_function("GnosisTomahawkAim", aim_callback);
        api->gsc()->register_function("GnosisTomahawkFlight", flight_callback);
        api->gsc()->register_function("GnosisMobBox", box_callback);
        api->gsc()->register_function("GnosisMobSpawn", spawn_callback);
        api->gsc()->register_function("GnosisMobSpawnNext", spawn_next_callback);
        ready = gsc_args::bindings_match();
        return ready;
    }

    void clear()
    {
        std::lock_guard lock(guard);
        current = View{};
    }

    View snapshot()
    {
        std::lock_guard lock(guard);
        return current;
    }
} // namespace mob
