#include "robot_aid.hpp"
#include <cmath>
#include <cstdio>

namespace
{
    int failures = 0;

    void check(bool good, const char* why)
    {
        if (!good)
        {
            ++failures;
            std::printf("FAIL: %s\n", why);
        }
    }
} // namespace

int main()
{
    check(robot::section_radius(1500, 0) == 1500, "the section through the centre is the full radius");
    check(std::abs(robot::section_radius(1500, 900) - 1200) < 0.01f, "3-4-5 section");
    check(std::abs(robot::section_radius(1500, -900) - 1200) < 0.01f, "below the centre too");
    check(robot::section_radius(1500, 1500) < 0 && robot::section_radius(1500, 2000) < 0, "a plane outside the sphere has no section");

    robot::Player player;
    player.valid = true;
    const nav::Vec3 foot{1000, 2000, 300};
    player.origin = nav::Vec3{1000 + 1200, 2000, 300 - 900};
    check(!robot::counts(player, foot), "exactly 1500 away does not count (vanilla tests < 1500)");
    player.origin = nav::Vec3{1000 + 1199, 2000, 300 - 900};
    check(robot::counts(player, foot), "just inside the sphere counts");
    player.head = 1;
    check(!robot::counts(player, foot), "inside a robot head never counts");
    player.head = -1;
    player.valid = false;
    check(!robot::counts(player, foot), "a downed player never counts");

    check(robot::fresh(100, 300, 250) && !robot::fresh(100, 400, 250) && !robot::fresh(0, 10, 250) && !robot::fresh(500, 400, 250),
          "freshness window");
    for (int r = 0; r < robot::robot_count; ++r)
    {
        const auto steps = robot::planned(r);
        check(steps.first && steps.count > 4 && steps.count < robot::max_steps, "every real route has a recorded walk");
        bool alternate = true, any_reach = false;
        for (int i = 0; i < steps.count; ++i)
        {
            if (i > 0 && steps.first[i].foot == steps.first[i - 1].foot) alternate = false;
            any_reach = any_reach || steps.first[i].reaches;
        }
        check(alternate, "recorded big steps alternate feet");
        check(any_reach, "every route has steps a player can be inside");
        const auto spot = robot::at(steps.first[1]);
        check(robot::off_table(r, 1, spot) == 0, "a step on its recorded spot is 0 off");
        check(std::abs(robot::off_table(r, 1, nav::Vec3{spot.x + 3, spot.y + 4, spot.z}) - 5) < 0.01f, "off_table is the 3D distance");
        check(robot::off_table(r, steps.count, spot) < 0 && robot::off_table(r, -1, spot) < 0,
              "step numbers past the recording have no spot");
    }
    check(!robot::planned(3).first && robot::planned(3).count == 0, "the intro walk has no recorded steps");

    robot::View view;
    const std::uint64_t now = 1000;
    for (auto& r : view.robots)
        r.updated = now;
    view.robots[0].walking = view.robots[2].walking = true;
    auto active = robot::active(view, now);
    check(active[0] && !active[1] && active[2], "without a hatch every walking robot is active");
    view.robots[2].hatch = 0;
    active = robot::active(view, now);
    check(!active[0] && !active[1] && active[2], "the walking robot with the hatch is the only active one");
    view.robots[2].updated = 1;
    active = robot::active(view, now);
    check(active[0] && !active[2], "a stale robot is not active");

    std::printf("Robot aid checks: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
