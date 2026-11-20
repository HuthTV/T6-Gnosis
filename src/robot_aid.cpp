#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <plutonium_sdk.hpp>
#include "robot_aid.hpp"
#include <atomic>
#include <mutex>
#include "gsc_args.hpp"

namespace robot
{
    namespace
    {
        std::mutex guard;
        std::atomic<bool> ready{false};
        View current;

        void robot_impl()
        {
            gsc_args::Arguments args;
            int id = 0, walking = 0, hatch = 0, open = 0;
            Robot robot;
            if (!args.capture(8) || !args.integer(0, id) || !args.integer(1, robot.route) || !args.integer(2, walking) ||
                !args.vector(3, robot.feet[0]) || !args.vector(4, robot.feet[1]) || !args.integer(5, hatch) || !args.integer(6, open) ||
                !args.integer(7, robot.checked) || !args.unchanged())
                return;
            if (id < 0 || id >= robot_count || robot.route < 0 || robot.route >= route_count || walking < 0 || walking > 1 || hatch < -1 ||
                hatch > 1 || open < 0 || open > 1 || robot.checked < 0 || robot.checked > max_steps)
                return;
            robot.walking = walking == 1;
            robot.hatch = hatch;
            robot.hatch_open = open == 1;
            robot.updated = GetTickCount64();
            std::lock_guard lock(guard);
            current.robots[static_cast<std::size_t>(id)] = robot;
        }

        void player_impl()
        {
            gsc_args::Arguments args;
            int head = 0, valid = 0, blood = 0, clue = 0;
            Player player;
            if (!args.capture(6) || !args.vector(0, player.origin) || !args.integer(1, head) || !args.integer(2, valid) ||
                !args.integer(3, blood) || !args.integer(4, clue) || !args.number(5, player.clue_ms) || !args.unchanged())
                return;
            if (head < -1 || head >= robot_count || valid < 0 || valid > 1 || blood < 0 || blood > 1 || clue < 0 || clue > 2 ||
                player.clue_ms < 0 || player.clue_ms > 10000)
                return;
            player.head = head;
            player.valid = valid == 1;
            player.zombie_blood = blood == 1;
            player.clue = static_cast<Clue>(clue);
            player.updated = GetTickCount64();
            std::lock_guard lock(guard);
            current.player = player;
        }

        void step_impl()
        {
            gsc_args::Arguments args;
            int id = 0, foot = 0, notified = 0;
            Step step;
            if (!args.capture(8) || !args.integer(0, id) || !args.integer(1, step.route) || !args.integer(2, step.index) ||
                !args.integer(3, foot) || !args.vector(4, step.trigger) || !args.vector(5, step.player) || !args.number(6, step.distance) ||
                !args.integer(7, notified) || !args.unchanged())
                return;
            if (id < 0 || id >= robot_count || step.route < 0 || step.route >= route_count || step.index < 0 || step.index >= max_steps ||
                foot < 0 || foot > 1 || step.distance < 0 || notified < 0 || notified > 1)
                return;
            step.notified = notified == 1;
            step.updated = GetTickCount64();
            std::lock_guard lock(guard);
            current.steps[static_cast<std::size_t>(id)][static_cast<std::size_t>(foot)] = step;
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

        void PLUTONIUM_CALLBACK robot_callback() { invoke<robot_impl>(); }

        void PLUTONIUM_CALLBACK player_callback() { invoke<player_impl>(); }

        void PLUTONIUM_CALLBACK step_callback() { invoke<step_impl>(); }
    } // namespace

    bool startup(plutonium::sdk::v1::iinterface* api)
    {
        ready = false;
        clear();
        if (!api) return false;
        // Registered before the check, as in staff_aid. Mismatched builds ignore the calls.
        api->gsc()->register_function("GnosisRobot", robot_callback);
        api->gsc()->register_function("GnosisRobotPlayer", player_callback);
        api->gsc()->register_function("GnosisRobotStep", step_callback);
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
} // namespace robot
