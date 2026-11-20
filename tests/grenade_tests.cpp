#include "grenade_aid.hpp"
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
    grenade::Path path;
    for (unsigned i = 0; i < grenade::max_points + 20; ++i)
        grenade::append(path, nav::Vec3{static_cast<float>(i), 0, 0}, 0);
    check(path.count == grenade::max_points - 1, "plain points leave the last slot free");
    grenade::append(path, nav::Vec3{-1, 0, 0}, grenade::bounce);
    check(path.count == grenade::max_points - 1, "a bounce does not take the last slot");
    grenade::append(path, nav::Vec3{-2, 0, 0}, grenade::burst);
    check(path.count == grenade::max_points && path.points[grenade::max_points - 1].x == -2 &&
              path.flags[grenade::max_points - 1] == grenade::burst,
          "the end point takes the last slot");
    grenade::append(path, nav::Vec3{-3, 0, 0}, grenade::rest);
    check(path.count == grenade::max_points && path.points[grenade::max_points - 1].x == -2, "a full path keeps its end");

    grenade::Path short_path;
    grenade::append(short_path, nav::Vec3{1, 2, 3}, 0);
    grenade::append(short_path, nav::Vec3{4, 5, 6}, grenade::rest);
    check(short_path.count == 2 && short_path.flags[1] == grenade::rest && short_path.points[1].z == 6, "short path in order");

    check(grenade::fresh(100, 300, 250) && !grenade::fresh(100, 400, 250) && !grenade::fresh(0, 10, 250) && !grenade::fresh(500, 400, 250),
          "freshness window");

    // pick: the path held back while it is the newest, else the preferred carried one.
    grenade::View view;
    auto publish = [&](int kind, std::uint64_t updated, bool pulled) {
        auto& path = view.aim[static_cast<std::size_t>(kind)];
        path = grenade::Path{};
        path.kind = kind;
        path.pulled = pulled;
        path.updated = updated;
        grenade::append(path, nav::Vec3{0, 0, 0}, 0);
    };
    check(grenade::pick(view, 1000, false) == nullptr, "nothing carried, nothing drawn");
    publish(grenade::frag, 1000, false);
    publish(grenade::beacon, 1000, false);
    check(grenade::pick(view, 1050, false)->kind == grenade::frag, "lethal first");
    check(grenade::pick(view, 1050, true)->kind == grenade::beacon, "tactical first");
    view.aim[grenade::beacon] = grenade::Path{};
    check(grenade::pick(view, 1050, true)->kind == grenade::frag, "the other one when the preferred is not carried");
    publish(grenade::beacon, 1000, false);
    publish(grenade::frag, 1100, true);
    check(grenade::pick(view, 1150, true)->kind == grenade::frag, "held back beats the priority");
    publish(grenade::beacon, 1200, false);
    check(grenade::pick(view, 1250, false)->kind == grenade::beacon, "the last frag thrown is not drawn once carried paths are newer");
    publish(grenade::frag, 1200, false);
    check(grenade::pick(view, 1250, false)->kind == grenade::frag, "frags left: back to the priority");
    check(grenade::pick(view, 2000, false) == nullptr, "stale paths are not drawn");
    std::printf("Grenade aid checks: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
