#pragma once
#include <array>
#include <cstdint>
#include "navigation.hpp"

namespace plutonium::sdk::v1
{
    class iinterface;
}

// Thrown-grenade aid: frag, EMP and the Origins G-strike. The GSC helper (gnosis_throw_aid)
// publishes:
//   GnosisThrowBegin( kind, seq, pulled )            20 Hz: a predicted path begins, for the grenade
//                                                    held back (pulled 1), else for each one carried
//   GnosisThrowPoint( seq, point, flags )            each 0.05 s step of it, and each bounce
//   GnosisThrowStrike( seq, index, point )           G-strike: where each of the five shells lands
//   GnosisThrowEnd( seq, fuse_left, valid )          commits the path. valid: G-strike in an enabled zone
//   GnosisThrowFlight( id, kind, point, event )      the real flight of each throw (Event)
//   GnosisThrowFlightStrike( id, index, point )      G-strike shells for the real landing spot
//   GnosisThrowRadii( kind, inner, outer )           1 Hz: the EMP's perk/power and zombie-stun radii
// The prediction's throw model is not fitted for these grenades yet (see the helper). The real
// flight is the engine's own: grenade.origin each frame plus the grenade_bounce, stationary and
// explode notifies.
namespace grenade
{
    enum Kind
    {
        none,
        frag,
        emp,
        beacon,
        kinds
    };

    // Point flags, shared with the helper (GNOSIS_THROW_*).
    enum Flag : std::uint8_t
    {
        bounce = 1,
        rest = 2,
        burst = 4
    };

    enum class Event
    {
        position,
        bounced,
        rested,
        exploded,
        alive,
        accepted,
        dud
    };

    // Vanilla _zm_weap_beacon: each shell kills within 200 units of where it lands.
    inline constexpr float strike_radius = 200;
    inline constexpr unsigned max_points = 128, strike_count = 5;

    struct Path
    {
        int kind = none, id = 0;
        unsigned count = 0;
        std::array<nav::Vec3, max_points> points{};
        std::array<std::uint8_t, max_points> flags{};
        unsigned strikes = 0;
        std::array<nav::Vec3, strike_count> strike{};
        // Prediction: fuse seconds left at the end point (-1 none), G-strike zone check.
        // Flight: set when it rested or exploded, and the game's G-strike verdict (-1 not yet).
        float fuse = -1;
        int verdict = -1;
        bool ended = false, pulled = false;
        std::uint64_t updated = 0;
    };

    struct View
    {
        std::array<Path, kinds> aim{}; // per kind, the path for a throw now
        Path thrown;                   // the last prediction before the latest real throw, for comparison
        Path flight;                   // the latest real throw
        float emp_inner = 0, emp_outer = 0;
    };

    inline constexpr std::uint64_t aim_ms = 250, flight_ms = 6000;

    inline bool fresh(std::uint64_t updated, std::uint64_t now, std::uint64_t lifetime)
    {
        return updated && now >= updated && now - updated <= lifetime;
    }

    inline bool lethal(int kind) { return kind == frag; }

    // The prediction to draw: the grenade held back, else the carried one the priority prefers
    // (lethal: frag, tactical: EMP or G-strike). A path held back counts only while it is the
    // newest, so the one just thrown does not linger over the next one carried.
    inline const Path* pick(const View& view, std::uint64_t now, bool tactical_first)
    {
        const Path* newest = nullptr;
        for (const auto& path : view.aim)
            if (path.count && fresh(path.updated, now, aim_ms) && (!newest || path.updated > newest->updated)) newest = &path;
        if (!newest || newest->pulled) return newest;
        const Path* fallback = nullptr;
        for (const auto& path : view.aim)
        {
            if (!path.count || path.pulled || !fresh(path.updated, now, aim_ms)) continue;
            if (lethal(path.kind) != tactical_first) return &path;
            if (!fallback) fallback = &path;
        }
        return fallback;
    }

    // Adds a point unless the path is full. The last slot is kept for an end point (rest or
    // burst), so a long path still ends where it ends.
    inline void append(Path& path, nav::Vec3 point, std::uint8_t flags)
    {
        const bool end = flags & (rest | burst);
        if (path.count >= max_points || (!end && path.count >= max_points - 1)) return;
        path.points[path.count] = point;
        path.flags[path.count] = flags;
        ++path.count;
    }

    bool startup(plutonium::sdk::v1::iinterface* api);
    void clear();
    View snapshot();
} // namespace grenade
