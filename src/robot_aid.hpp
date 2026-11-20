#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include "navigation.hpp"
#include "robot_routes.hpp"

namespace plutonium::sdk::v1
{
    class iinterface;
}

// Origins giant robot footsteps and Samantha's wind staff clue. Vanilla (zm_tomb_giant_robot):
// each robot's two stomp kill triggers ride its feet (linked to TAG_ATTACH_HATCH_LE / _RI, 72 up).
// On every big footstep rumble_and_shake waits 0.2 s, then sends sam_clue_giant for each valid
// player outside a robot head whose 3D distance to that foot's trigger is under clue_radius
// (farther away it only shakes the screen). zm_tomb_vo watch_one_shot_samantha_clue takes
// that notify, waits 10 s and plays the wind clue if the player is in zombie blood.
// The GSC helper (gnosis_robot_aid) publishes:
//   GnosisRobot( id, route, walking, left, right, hatch, hatch_open, checked )          per robot, 20 Hz
//   GnosisRobotPlayer( origin, head, valid, zombie_blood, clue, clue_ms )                20 Hz
//   GnosisRobotStep( id, route, index, foot, trigger, player, distance, notified )      each big footstep, at the check
// route is the walk's path: the robot id, or 3 for robot 1's intro walk. Each walk is
// animscripted from a fixed align struct with fixed animations, so big step number index of a
// route lands on the same spot every walk: robot_routes.hpp holds every big step of routes 0
// to 2, recorded once. The intro walk has no table (at game start no clue can play). checked
// counts this walk's steps that had their check. left and right are the trigger origins. hatch is the foot index (0 left, 1 right) of the foot
// with the sole hatch, or -1. head is the robot id whose head the player is in, or -1. clue is
// the helper's copy of the clue thread: 0 waiting for a step, 1 in its 10 s wait (clue_ms left),
// 2 ended (all wind pieces found). notified: vanilla sent sam_clue_giant at that check.
namespace robot
{
    inline constexpr float clue_radius = 1500;
    inline constexpr int robot_count = 3, route_count = 4, max_steps = 256;
    inline constexpr std::uint64_t live_ms = 250, highlight_ms = 1500, step_ms = 4000;

    enum class Clue
    {
        idle = 0,
        waiting = 1,
        ended = 2
    };

    struct Robot
    {
        int route = 0, checked = 0;
        bool walking = false;
        std::array<nav::Vec3, 2> feet{};
        int hatch = -1;
        bool hatch_open = false;
        std::uint64_t updated = 0;
    };

    struct Player
    {
        nav::Vec3 origin;
        int head = -1;
        bool valid = false, zombie_blood = false;
        Clue clue = Clue::idle;
        float clue_ms = 0;
        std::uint64_t updated = 0;
    };

    struct Step
    {
        int route = 0, index = 0;
        nav::Vec3 trigger, player;
        float distance = 0;
        bool notified = false;
        std::uint64_t updated = 0;
    };

    struct View
    {
        std::array<Robot, robot_count> robots{};
        Player player;
        std::array<std::array<Step, 2>, robot_count> steps{};
    };

    // A route's recorded big steps (robot_routes.hpp), none for the intro walk.
    struct Steps
    {
        const Planned* first = nullptr;
        int count = 0;
    };

    inline Steps planned(int route)
    {
        switch (route)
        {
        case 0: return {route_0.data(), static_cast<int>(route_0.size())};
        case 1: return {route_1.data(), static_cast<int>(route_1.size())};
        case 2: return {route_2.data(), static_cast<int>(route_2.size())};
        default: return {};
        }
    }

    inline nav::Vec3 at(const Planned& p) { return {p.x, p.y, p.z}; }

    // A step farther than this from its recorded spot means the recording no longer matches.
    inline constexpr float table_tolerance = 8;

    // How far above the ground ring the drawn dome of a clue sphere reaches.
    inline constexpr float dome_height = 600;

    // Distance from a step seen at its check to the recorded spot of that step, or -1 when the
    // table has none (the intro walk, or a step number past the recording).
    inline float off_table(int route, int index, nav::Vec3 trigger)
    {
        const auto steps = planned(route);
        if (index < 0 || index >= steps.count) return -1;
        const auto d = trigger - at(steps.first[index]);
        return std::sqrt(nav::dot(d, d));
    }

    inline bool fresh(std::uint64_t updated, std::uint64_t now, std::uint64_t lifetime)
    {
        return updated && now >= updated && now - updated <= lifetime;
    }

    // Radius of a sphere's cross-section at height offset dz from its centre, or -1 when the
    // plane misses it. The clue test is 3D, so at the player's height the boundary is this circle.
    inline float section_radius(float radius, float dz)
    {
        const float r2 = radius * radius - dz * dz;
        return r2 > 0 ? std::sqrt(r2) : -1;
    }

    // The robots the aid follows: walking ones with a hatch (the robot whose foot holds the
    // wind staff piece, one per walk cycle, also in three-robot rounds), else every walking
    // one (robot 1's intro walk has no hatch).
    inline std::array<bool, robot_count> active(const View& view, std::uint64_t now)
    {
        std::array<bool, robot_count> walking{}, hatch{};
        bool any_hatch = false;
        for (std::size_t i = 0; i < robot_count; ++i)
        {
            const auto& r = view.robots[i];
            walking[i] = fresh(r.updated, now, live_ms) && r.walking;
            hatch[i] = walking[i] && r.hatch >= 0;
            any_hatch = any_hatch || hatch[i];
        }
        return any_hatch ? hatch : walking;
    }

    // Whether a step of this foot would send the clue notify now: vanilla's test, distance
    // strictly under the radius, a valid player outside every robot head.
    inline bool counts(const Player& player, nav::Vec3 trigger)
    {
        const auto d = player.origin - trigger;
        return player.valid && player.head < 0 && nav::dot(d, d) < clue_radius * clue_radius;
    }

    bool startup(plutonium::sdk::v1::iinterface* api);
    void clear();
    View snapshot();
} // namespace robot
