// Map aids: Origins wind and fire puzzles and giant robot footsteps, Mob of the Dead blue skulls, shock boxes and
// Afterlife spawns, TranZit Avogadro, thrown grenades. Each draws fresh samples its *_aid module received
// from the GSC helper, at render rate.
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>
#include "overlay_shared.hpp"
#include "fonts.hpp"
#include "playable_area.hpp"
#include "zombies.hpp"

namespace overlay
{
    namespace
    {
        // Torch number shown to the player for the helper's torch index (0-8). User numbering
        // (2026-10-13). The gaps are intentional (11, and no 8 or 10).
        int torch_number(int index)
        {
            constexpr int numbers[] = {6, 5, 7, 1, 2, 3, 11, 9, 4};
            return index >= 0 && index < static_cast<int>(std::size(numbers)) ? numbers[index] : index + 1;
        }

        // Translucent orb shaded like a lit sphere: a dark base over the silhouette, then
        // layers shrinking toward a highlight up and to the left that brighten toward white,
        // a darker rim and a small specular spot.
        void draw_orb(ImDrawList* list, const std::vector<ImVec2>& outline, ImU32 color, bool strong)
        {
            const int points = static_cast<int>(outline.size());
            ImVec2 centre{};
            for (const auto& p : outline)
            {
                centre.x += p.x;
                centre.y += p.y;
            }
            centre.x /= points;
            centre.y /= points;
            float radius = 0;
            for (const auto& p : outline)
                radius += std::sqrt((p.x - centre.x) * (p.x - centre.x) + (p.y - centre.y) * (p.y - centre.y));
            radius /= points;
            const int r = static_cast<int>((color >> IM_COL32_R_SHIFT) & 0xFF), g = static_cast<int>((color >> IM_COL32_G_SHIFT) & 0xFF),
                      b = static_cast<int>((color >> IM_COL32_B_SHIFT) & 0xFF);
            const int base_alpha = strong ? 60 : 35;
            list->AddConvexPolyFilled(outline.data(), points, IM_COL32(r * 45 / 100, g * 45 / 100, b * 45 / 100, base_alpha));
            const ImVec2 light(centre.x - radius * 0.35f, centre.y - radius * 0.4f);
            std::vector<ImVec2> layer(outline.size());
            constexpr int layers = 7;
            for (int k = 0; k < layers; ++k)
            {
                const float scale = 0.92f - 0.13f * k, mix = static_cast<float>(k) / layers * 0.55f;
                for (int i = 0; i < points; ++i)
                    layer[static_cast<std::size_t>(i)] = ImVec2(light.x + (outline[static_cast<std::size_t>(i)].x - light.x) * scale,
                                                                light.y + (outline[static_cast<std::size_t>(i)].y - light.y) * scale);
                const auto shade = IM_COL32(static_cast<int>(r + (255 - r) * mix), static_cast<int>(g + (255 - g) * mix),
                                            static_cast<int>(b + (255 - b) * mix), strong ? 22 : 14);
                list->AddConvexPolyFilled(layer.data(), points, shade);
            }
            list->AddPolyline(outline.data(), points, IM_COL32(r * 35 / 100, g * 35 / 100, b * 35 / 100, strong ? 190 : 120),
                              ImDrawFlags_Closed, 2.5f);
            list->AddPolyline(outline.data(), points, (color & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, strong ? 170 : 110),
                              ImDrawFlags_Closed, 1.2f);
            const ImVec2 spot(centre.x - radius * 0.42f, centre.y - radius * 0.48f);
            list->AddEllipseFilled(spot, ImVec2(radius * 0.16f, radius * 0.10f), IM_COL32(255, 255, 255, strong ? 90 : 50), -0.6f, 16);
        }

        // Translucent solid sphere: fill its silhouette, the circle where view rays graze it,
        // R*R/L in front of the centre with radius R*sqrt(L*L-R*R)/L (L = eye distance).
        // Skipped from inside the sphere, where it would cover the whole screen. A fully
        // visible silhouette is a shaded orb. One cut by the screen edge keeps a flat fill
        // (the orb shading assumes a whole outline). Returns the silhouette's top on screen,
        // for a label above it, or -1 when nothing was drawn.
        float draw_sphere(ImDrawList* list, const nav::Camera& cam, nav::Vec3 centre, float radius, ImU32 color, bool strong, float sx,
                          float sy)
        {
            const auto to = centre - cam.origin;
            const float distance = std::sqrt(nav::dot(to, to));
            if (distance <= radius + 1) return -1;
            const auto n = to * (1.0f / distance);
            auto cross = [](nav::Vec3 a, nav::Vec3 b) {
                return nav::Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
            };
            auto u = cross(n, std::abs(n.z) < 0.9f ? nav::Vec3{0, 0, 1} : nav::Vec3{1, 0, 0});
            u = u * (1.0f / std::sqrt(nav::dot(u, u)));
            const auto v = cross(n, u);
            const auto centre_ring = centre - n * (radius * radius / distance);
            const float ring_radius = radius * std::sqrt(distance * distance - radius * radius) / distance;
            constexpr int segments = 48;
            std::vector<nav::Vec3> ring;
            ring.reserve(segments);
            for (int s = 0; s < segments; ++s)
            {
                const float a = 6.2831853f * s / segments;
                ring.push_back(nav::camera_space(cam, centre_ring + (u * std::cos(a) + v * std::sin(a)) * ring_radius));
            }
            std::vector<ImVec2> pixels;
            for (const auto p : nav::clip_polygon(cam, ring))
                pixels.push_back(screen(nav::project(cam, p), sx, sy));
            if (pixels.size() < 3) return -1;
            const int points = static_cast<int>(pixels.size());
            if (pixels.size() == ring.size())
                draw_orb(list, pixels, color, strong);
            else
            {
                list->AddConvexPolyFilled(pixels.data(), points, (color & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, strong ? 55 : 30));
                list->AddPolyline(pixels.data(), points, (color & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 150), ImDrawFlags_Closed, 1.5f);
            }
            float top = pixels[0].y;
            for (const auto& p : pixels)
                top = std::min(top, p.y);
            return top;
        }

        // Screen-space occlusion for the torch spheres: a nearer torch's silhouette (a convex
        // polygon) hides what lies behind it. Most triangles are wholly outside or wholly
        // inside a silhouette and are decided by a point test. Only the ones its edge crosses
        // are clipped, against each silhouette edge in turn (outside parts kept). No heap use
        // per triangle: the per-frame cost was what made overlapping torches lag.
        using Shape = std::vector<ImVec2>;

        struct Occluder
        {
            Shape points;
            float winding = 1;
            ImVec2 lo{}, hi{};

            explicit Occluder(Shape s) : points(std::move(s))
            {
                float sum = 0;
                lo = hi = points[0];
                for (std::size_t i = 0; i < points.size(); ++i)
                {
                    const auto& a = points[i];
                    const auto& b = points[(i + 1) % points.size()];
                    sum += a.x * b.y - b.x * a.y;
                    lo.x = std::min(lo.x, a.x);
                    lo.y = std::min(lo.y, a.y);
                    hi.x = std::max(hi.x, a.x);
                    hi.y = std::max(hi.y, a.y);
                }
                winding = sum < 0 ? -1.0f : 1.0f;
            }

            float side(std::size_t e, ImVec2 p) const
            {
                const auto& a = points[e];
                const auto& b = points[(e + 1) % points.size()];
                return winding * ((b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x));
            }

            bool contains(ImVec2 p) const
            {
                if (p.x < lo.x || p.x > hi.x || p.y < lo.y || p.y > hi.y) return false;
                for (std::size_t e = 0; e < points.size(); ++e)
                    if (side(e, p) < 0) return false;
                return true;
            }

            bool apart(ImVec2 plo, ImVec2 phi) const { return phi.x < lo.x || plo.x > hi.x || phi.y < lo.y || plo.y > hi.y; }
        };

        // Small fixed-size polygon for clipping (a triangle cut by a convex polygon stays small).
        struct Poly
        {
            std::array<ImVec2, 16> p{};
            int n = 0;

            void add(ImVec2 v)
            {
                if (n < 16) p[static_cast<std::size_t>(n++)] = v;
            }
        };

        // Fills the parts of poly outside every occluder from index first on.
        void fill_visible(ImDrawList* list, const Poly& poly, const std::vector<const Occluder*>& occluders, std::size_t first, ImU32 color)
        {
            ImVec2 plo = poly.p[0], phi = poly.p[0];
            for (int i = 1; i < poly.n; ++i)
            {
                const auto v = poly.p[static_cast<std::size_t>(i)];
                plo.x = std::min(plo.x, v.x);
                plo.y = std::min(plo.y, v.y);
                phi.x = std::max(phi.x, v.x);
                phi.y = std::max(phi.y, v.y);
            }
            for (std::size_t k = first; k < occluders.size(); ++k)
            {
                const auto& o = *occluders[k];
                if (o.apart(plo, phi)) continue;
                int inside = 0;
                for (int i = 0; i < poly.n; ++i)
                    inside += o.contains(poly.p[static_cast<std::size_t>(i)]);
                if (inside == poly.n) return; // convex occluder holds every corner: all hidden
                // Clip: split off the part outside each edge. The rest continues to the next edge.
                Poly rest = poly;
                for (std::size_t e = 0; e < o.points.size() && rest.n >= 3; ++e)
                {
                    float d[16];
                    bool any_out = false;
                    for (int i = 0; i < rest.n; ++i)
                    {
                        d[i] = o.side(e, rest.p[static_cast<std::size_t>(i)]);
                        any_out |= d[i] < 0;
                    }
                    if (!any_out) continue;
                    Poly in, out;
                    for (int i = 0; i < rest.n; ++i)
                    {
                        const int j = (i + 1) % rest.n;
                        const auto a = rest.p[static_cast<std::size_t>(i)], b = rest.p[static_cast<std::size_t>(j)];
                        if (d[i] >= 0)
                            in.add(a);
                        else
                            out.add(a);
                        if ((d[i] >= 0) != (d[j] >= 0))
                        {
                            const float t = d[i] / (d[i] - d[j]);
                            const ImVec2 x(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
                            in.add(x);
                            out.add(x);
                        }
                    }
                    if (out.n >= 3) fill_visible(list, out, occluders, k + 1, color);
                    rest = in;
                }
                return; // what is left is inside this occluder
            }
            list->AddConvexPolyFilled(poly.p.data(), poly.n, color);
        }

        // Draws the parts of segment a-b outside every occluder from index first on.
        void line_visible(ImDrawList* list, ImVec2 a, ImVec2 b, const std::vector<const Occluder*>& occluders, std::size_t first,
                          ImU32 color, float thickness)
        {
            const ImVec2 plo(std::min(a.x, b.x), std::min(a.y, b.y)), phi(std::max(a.x, b.x), std::max(a.y, b.y));
            for (std::size_t k = first; k < occluders.size(); ++k)
            {
                const auto& o = *occluders[k];
                if (o.apart(plo, phi)) continue;
                float t0 = 0, t1 = 1;
                bool outside = false;
                for (std::size_t e = 0; e < o.points.size() && !outside; ++e)
                {
                    const float da = o.side(e, a), db = o.side(e, b);
                    if (da < 0 && db < 0)
                        outside = true;
                    else if (da < 0)
                        t0 = std::max(t0, da / (da - db));
                    else if (db < 0)
                        t1 = std::min(t1, da / (da - db));
                }
                if (outside || t0 >= t1) continue;
                auto at = [&](float t) { return ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); };
                if (t0 > 0) line_visible(list, a, at(t0), occluders, k + 1, color, thickness);
                if (t1 < 1) line_visible(list, at(t1), b, occluders, k + 1, color, thickness);
                return;
            }
            list->AddLine(a, b, color, thickness);
        }

        // Silhouette of a sphere on screen (see draw_sphere). Empty from inside the sphere.
        Shape sphere_outline(const nav::Camera& cam, nav::Vec3 centre, float radius, float sx, float sy)
        {
            const auto to = centre - cam.origin;
            const float distance = std::sqrt(nav::dot(to, to));
            if (distance <= radius + 1) return {};
            const auto n = to * (1.0f / distance);
            auto u = area::cross(n, std::abs(n.z) < 0.9f ? nav::Vec3{0, 0, 1} : nav::Vec3{1, 0, 0});
            u = u * (1.0f / std::sqrt(nav::dot(u, u)));
            const auto v = area::cross(n, u);
            const auto centre_ring = centre - n * (radius * radius / distance);
            const float ring_radius = radius * std::sqrt(distance * distance - radius * radius) / distance;
            constexpr int segments = 64;
            std::vector<nav::Vec3> ring;
            ring.reserve(segments);
            for (int s = 0; s < segments; ++s)
            {
                const float a = 6.2831853f * s / segments;
                ring.push_back(nav::camera_space(cam, centre_ring + (u * std::cos(a) + v * std::sin(a)) * ring_radius));
            }
            Shape pixels;
            for (const auto p : nav::clip_polygon(cam, ring))
                pixels.push_back(screen(nav::project(cam, p), sx, sy));
            if (pixels.size() < 3) return {};
            return pixels;
        }

        // Torch sphere (user request 2026-10-20): the three axis great circles drawn in full and
        // the eight sections between them filled with flat translucent colour, alternating
        // slightly in brightness. Far side first, near side over it, so the overlap reads as
        // depth. Parts covered by a nearer torch's silhouette (occluders) are not drawn.
        void draw_octant_sphere(ImDrawList* list, const nav::Camera& cam, nav::Vec3 centre, float radius, ImU32 color, bool strong,
                                const Shape& outline, const std::vector<const Occluder*>& occluders, float sx, float sy)
        {
            if (outline.empty()) return;
            const int r = static_cast<int>((color >> IM_COL32_R_SHIFT) & 0xFF), g = static_cast<int>((color >> IM_COL32_G_SHIFT) & 0xFF),
                      b = static_cast<int>((color >> IM_COL32_B_SHIFT) & 0xFF);
            auto tint = [&](float k, int alpha) {
                return IM_COL32(static_cast<int>(r * k), static_cast<int>(g * k), static_cast<int>(b * k), alpha);
            };
            auto facing = [&](nav::Vec3 direction) { return nav::dot(direction, cam.origin - (centre + direction * radius)) > 0; };
            auto pixel = [&](nav::Vec3 direction, ImVec2& out) {
                const auto p = nav::camera_space(cam, centre + direction * radius);
                if (p.z < 1) return false;
                out = screen(nav::project(cam, p), sx, sy);
                return true;
            };
            auto fill = [&](const Poly& poly, ImU32 c) { fill_visible(list, poly, occluders, 0, c); };
            auto line = [&](ImVec2 a, ImVec2 c2, ImU32 c, float thickness) { line_visible(list, a, c2, occluders, 0, c, thickness); };
            // Triangles share exact edges. Anti-aliased fringes would show as seams.
            const auto flags = list->Flags;
            list->Flags &= ~ImDrawListFlags_AntiAliasedFill;
            constexpr int steps = 6;
            for (int pass = 0; pass < 2; ++pass)
            {
                const bool front = pass == 1;
                for (int octant = 0; octant < 8; ++octant)
                {
                    const nav::Vec3 a{octant & 1 ? radius : -radius, 0, 0}, bb{0, octant & 2 ? radius : -radius, 0},
                        c{0, 0, octant & 4 ? radius : -radius};
                    const bool light = ((octant & 1) ^ ((octant >> 1) & 1) ^ ((octant >> 2) & 1)) != 0;
                    const auto shade = tint(light ? 1.0f : 0.72f, front ? (strong ? 46 : 28) : (strong ? 26 : 16));
                    auto point = [&](int i, int j) {
                        auto p = a * static_cast<float>(steps - i - j) + bb * static_cast<float>(i) + c * static_cast<float>(j);
                        return p * (1.0f / std::sqrt(nav::dot(p, p)));
                    };
                    auto triangle = [&](nav::Vec3 p, nav::Vec3 q, nav::Vec3 s) {
                        auto mid = p + q + s;
                        mid = mid * (1.0f / std::sqrt(nav::dot(mid, mid)));
                        if (facing(mid) != front) return;
                        Poly poly;
                        poly.n = 3;
                        if (!pixel(p, poly.p[0]) || !pixel(q, poly.p[1]) || !pixel(s, poly.p[2])) return;
                        fill(poly, shade);
                    };
                    for (int i = 0; i < steps; ++i)
                        for (int j = 0; i + j < steps; ++j)
                        {
                            triangle(point(i, j), point(i + 1, j), point(i, j + 1));
                            if (i + j + 1 < steps) triangle(point(i + 1, j), point(i + 1, j + 1), point(i, j + 1));
                        }
                }
                list->Flags = flags;
                // Great circles in the three axis planes: thin and faint behind, bright in front.
                constexpr int segments = 72;
                for (int axis = 0; axis < 3; ++axis)
                    for (int s = 0; s < segments; ++s)
                    {
                        auto dir = [&](int k) {
                            const float t = 6.2831853f * k / segments, x = std::cos(t), y = std::sin(t);
                            return axis == 0 ? nav::Vec3{0, x, y} : axis == 1 ? nav::Vec3{x, 0, y} : nav::Vec3{x, y, 0};
                        };
                        const auto p = dir(s), q = dir(s + 1);
                        if (facing((p + q) * 0.5f) != front) continue;
                        ImVec2 pa, pb;
                        if (!pixel(p, pa) || !pixel(q, pb)) continue;
                        line(pa, pb, front ? tint(1.0f, strong ? 230 : 170) : tint(0.8f, strong ? 90 : 60), front ? 1.6f : 1.0f);
                    }
                if (!front) list->Flags &= ~ImDrawListFlags_AntiAliasedFill;
            }
            list->Flags = flags;
            for (std::size_t i = 0; i < outline.size(); ++i)
                line(outline[i], outline[(i + 1) % outline.size()], tint(1.0f, strong ? 200 : 130), 1.4f);
        }

        // TranZit Avogadro aid (avogadro_aid.hpp). His body is the native zombie marker with the
        // helper's entity number, so everything follows him at render rate.
        bool avogadro_body(const avogadro::Status& s, const nav::Graph& g, zombies::Marker& marker, nav::Vec3& origin)
        {
            if (s.number < 0 || s.in_storm()) return false;
            const auto z = zombies::snapshot();
            if (!z || GetTickCount64() - z->updated > 200 || !z->map || z->map->map != g.map || z->map->base != g.base ||
                z->map->data != g.data)
                return false;
            for (const auto& m : z->markers)
                if (m.number == s.number)
                {
                    marker = m;
                    return zombies::still_living(m, origin);
                }
            return false;
        }

        ImU32 tone_color(avogadro::Tone tone)
        {
            switch (tone)
            {
            case avogadro::Tone::title: return IM_COL32(110, 205, 255, 255);
            case avogadro::Tone::good: return IM_COL32(80, 235, 120, 255);
            case avogadro::Tone::warn: return IM_COL32(255, 200, 70, 255);
            case avogadro::Tone::bad: return IM_COL32(255, 95, 95, 255);
            case avogadro::Tone::dim: return IM_COL32(175, 180, 190, 235);
            default: return IM_COL32(235, 235, 240, 255);
            }
        }
    } // namespace

    // Origins wind puzzle 2 aid. Boxes, arrows and the verdict come from the GSC helper's
    // traces (staff_aid.hpp). This only draws fresh samples, at render rate.
    // details adds the verdict label and live readout to the boxes, arrows and
    // landing ring.
    void draw_wind(ImDrawList* list, const nav::Camera& cam, const staff::View& view, float width, float height, float sx, float sy,
                   bool details)
    {
        const auto now = GetTickCount64();
        auto segment = [&](nav::Vec3 a, nav::Vec3 b, ImU32 color, float thickness) {
            auto p = nav::camera_space(cam, a), q = nav::camera_space(cam, b);
            if (nav::clip_segment(cam, p, q))
                list->AddLine(screen(nav::project(cam, p), sx, sy), screen(nav::project(cam, q), sx, sy), color, thickness);
        };
        const unsigned edges[][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (const auto& s : view.smokes)
        {
            if (!staff::fresh(s.updated, now, staff::smoke_lifetime_ms)) continue;
            const auto color = s.cooling  ? IM_COL32(255, 215, 60, 255)
                               : s.solved ? IM_COL32(60, 230, 110, 255)
                                          : IM_COL32(255, 70, 70, 255);
            const auto lo = s.mins, hi = s.maxs;
            const nav::Vec3 corners[] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
                                         {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
            for (const auto& edge : edges)
                segment(corners[edge[0]], corners[edge[1]], color, 2.0f);
            // Arrow from the box centre toward the destination: shots travelling this way pass.
            // Always drawn with the box (user request). Only the text is behind details.
            const auto center = (lo + hi) * 0.5f, tip = center + s.to_dest * 160.0f;
            segment(center, tip, color, 2.0f);
            nav::Vec3 side{-s.to_dest.y, s.to_dest.x, 0};
            const float length = std::sqrt(nav::dot(side, side));
            if (length > 0.01f)
            {
                side = side * (15.0f / length);
                segment(tip, tip - s.to_dest * 30.0f + side, color, 2.0f);
                segment(tip, tip - s.to_dest * 30.0f - side, color, 2.0f);
            }
        }
        const auto& aim = view.aim;
        if (!staff::fresh(aim.updated, now, staff::aim_lifetime_ms)) return;
        ImU32 color = IM_COL32(170, 170, 170, 255);
        const char* verdict = "miss";
        switch (aim.verdict)
        {
        case staff::Verdict::fail:
            color = IM_COL32(255, 70, 70, 255);
            verdict = "fails";
            break;
        case staff::Verdict::pass:
            color = IM_COL32(60, 230, 110, 255);
            verdict = "solves";
            break;
        case staff::Verdict::edge:
            color = IM_COL32(255, 215, 60, 255);
            verdict = "grazes (uncertain)";
            break;
        default: break;
        }
        const auto at = nav::camera_space(cam, aim.point);
        if (nav::point_visible(cam, at))
        {
            const auto p = screen(nav::project(cam, at), sx, sy);
            // Hollow ring so the impact spot stays visible. Black underlay for contrast.
            list->AddCircle(p, 14, IM_COL32(0, 0, 0, 255), 0, 5);
            list->AddCircle(p, 14, color, 0, 3);
        }
        if (!details) return;
        char label[96]{};
        if (aim.smoke < 0)
            std::snprintf(label, sizeof(label), "Wind: miss");
        else
        {
            const char* hit = aim.hit == staff::Hit::direct ? " (direct)" : aim.hit == staff::Hit::splash ? " (splash)" : "";
            const bool cooling = view.smokes[static_cast<std::size_t>(aim.smoke)].cooling;
            std::snprintf(label, sizeof(label), "Wind: %s smoke %d%s%s", verdict, aim.smoke + 1, hit,
                          cooling ? " - wait, ignoring hits" : "");
        }
        const float scale = height / 1080.0f, font_size = 19.5f * scale;
        const auto size = fonts::get(font_size)->CalcTextSizeA(font_size, 10000, 0, label);
        const ImVec2 pos(width * 0.5f - size.x * 0.5f, height * 0.5f + 60.0f * scale);
        list->AddText(fonts::get(font_size), font_size, ImVec2(pos.x + scale, pos.y + scale), IM_COL32(0, 0, 0, 200), label);
        list->AddText(fonts::get(font_size), font_size, pos, color, label);
        // Live readout of the verdict's inputs, angles first. Vanilla passes when
        // dot( to_dest, direction ) > cos( script_int ). Direction is not normalized, so the
        // effective angle limit is acos( cos( script_int ) / |direction| ), not script_int.
        std::array<std::array<char, 112>, 7> lines{};
        unsigned count = 0;
        auto line = [&](const char* format, auto... values) {
            if (count < lines.size()) std::snprintf(lines[count++].data(), lines[0].size(), format, values...);
        };
        float yaw = 0, pitch = 0;
        staff::angles(aim.forward, yaw, pitch);
        line("Aim          yaw %6.1f   pitch %5.1f", yaw, pitch);
        const bool smoke_fresh =
            aim.smoke >= 0 && staff::fresh(view.smokes[static_cast<std::size_t>(aim.smoke)].updated, now, staff::smoke_lifetime_ms);
        if (aim.hit != staff::Hit::none && smoke_fresh)
        {
            const auto& smoke = view.smokes[static_cast<std::size_t>(aim.smoke)];
            const float length = std::sqrt(nav::dot(aim.direction, aim.direction));
            staff::angles(aim.direction, yaw, pitch);
            line("Judged       yaw %6.1f   pitch %5.1f   %s |%.1f|", yaw, pitch, aim.hit == staff::Hit::direct ? "velocity" : "splash",
                 length);
            staff::angles(smoke.to_dest, yaw, pitch);
            line("To dest      yaw %6.1f   pitch %5.1f", yaw, pitch);
            const float angle = staff::angle_between(aim.direction, smoke.to_dest);
            const float nominal = std::acos(std::clamp(aim.threshold, -1.0f, 1.0f)) * 57.2957795f;
            const float limit = length > 0 ? std::acos(std::clamp(aim.threshold / length, -1.0f, 1.0f)) * 57.2957795f : 0;
            line("Angle        %5.1f deg   limit %5.2f (nominal %.0f)   margin %+.1f", angle, limit, nominal, limit - angle);
            line("dot %.2f  vs  cos(script_int) %.3f", nav::dot(smoke.to_dest, aim.direction), aim.threshold);
        }
        else if (aim.verdict == staff::Verdict::edge)
            line("Within %d units of smoke %d's box: traces and projectile can disagree", 2, aim.smoke + 1);
        line("World %.1f   brush %.1f   (bullettrace / physicstrace, units)", aim.world_dist, aim.brush_dist);
        if (aim.smoke >= 0)
            line(aim.cooldown_ms > 0 ? "Smoke %d ignoring hits for %.2f s" : "Smoke %d accepting hits", aim.smoke + 1,
                 aim.cooldown_ms / 1000.0f);
        const float detail_size = 14.25f * scale, step = detail_size + 3 * scale;
        float block = 0;
        for (unsigned i = 0; i < count; ++i)
            block = std::max(block, fonts::get(detail_size)->CalcTextSizeA(detail_size, 10000, 0, lines[i].data()).x);
        const ImVec2 top(width * 0.5f - block * 0.5f, pos.y + font_size + 8 * scale);
        list->AddRectFilled(ImVec2(top.x - 8 * scale, top.y - 4 * scale),
                            ImVec2(top.x + block + 8 * scale, top.y + count * step + 2 * scale), IM_COL32(0, 0, 0, 140), 4 * scale);
        for (unsigned i = 0; i < count; ++i)
            list->AddText(fonts::get(detail_size), detail_size, ImVec2(top.x, top.y + i * step), IM_COL32(230, 232, 236, 255),
                          lines[i].data());
    }

    // Origins fire staff aid: the three predicted explosions whenever the fire staff is held, and
    // during fire puzzle 2 the torch spheres, from the GSC helper's traces (staff_aid.hpp). Fresh
    // samples only, drawn at render rate.
    // Torch labels (torch_number) are always shown. Details adds the summary line and live
    // readout to the landing rings.
    void draw_fire(ImDrawList* list, const nav::Camera& cam, const staff::View& view, float width, float height, float sx, float sy,
                   bool details)
    {
        const auto now = GetTickCount64();
        auto torch_color = [](const staff::Torch& t) {
            if (t.correct) return t.lit ? IM_COL32(60, 230, 110, 255) : IM_COL32(255, 160, 40, 255);
            return t.lit ? IM_COL32(255, 70, 70, 255) : IM_COL32(150, 150, 150, 150);
        };
        unsigned correct = 0, correct_lit = 0, wrong_lit = 0, torches = 0;
        // Far to near, each torch hidden where a nearer torch's silhouette covers it.
        std::vector<std::size_t> order;
        std::array<Shape, 9> outlines{};
        std::array<float, 9> distances{};
        std::array<std::optional<Occluder>, 9> shapes{};
        for (std::size_t i = 0; i < view.torches.size(); ++i)
        {
            const auto& t = view.torches[i];
            if (!staff::fresh(t.updated, now, staff::smoke_lifetime_ms)) continue;
            const auto to = t.origin - cam.origin;
            distances[i] = nav::dot(to, to);
            outlines[i] = sphere_outline(cam, t.origin, staff::torch_radius, sx, sy);
            if (!outlines[i].empty()) shapes[i].emplace(outlines[i]);
            order.push_back(i);
        }
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return distances[a] > distances[b]; });
        for (std::size_t k = 0; k < order.size(); ++k)
        {
            const std::size_t i = order[k];
            const auto& t = view.torches[i];
            ++torches;
            correct += t.correct;
            correct_lit += t.correct && t.lit;
            wrong_lit += !t.correct && t.lit;
            const auto color = torch_color(t);
            std::vector<const Occluder*> occluders;
            for (std::size_t m = k + 1; m < order.size(); ++m)
                if (!outlines[order[m]].empty()) occluders.push_back(&*shapes[order[m]]);
            draw_octant_sphere(list, cam, t.origin, staff::torch_radius, color, t.correct || t.lit, outlines[i], occluders, sx, sy);
            const auto centre = nav::camera_space(cam, t.origin);
            // Small solid ball at the centre (the actual test point) for depth perspective:
            // it shrinks with distance like a 4-unit sphere would.
            bool hidden = false;
            if (nav::point_visible(cam, centre))
                for (const auto* o : occluders)
                    if (o->contains(screen(nav::project(cam, centre), sx, sy))) hidden = true;
            if (nav::point_visible(cam, centre) && !hidden)
            {
                const auto p = screen(nav::project(cam, centre), sx, sy);
                const float pixels = std::clamp(4.0f / (centre.z * cam.tan_y) * cam.height * 0.5f * sy, 2.0f, 10.0f);
                list->AddCircleFilled(p, pixels + 1, IM_COL32(0, 0, 0, 200));
                list->AddCircleFilled(p, pixels, (color & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 255));
            }
            // Label hovering over the sphere's top point (centre + radius up), the same anchor near,
            // far and from inside (user request 2026-10-20), on a dark plate over bright walls.
            const auto top = nav::camera_space(cam, t.origin + nav::Vec3{0, 0, staff::torch_radius});
            if (nav::point_visible(cam, top))
            {
                char text[48]{};
                const int number = torch_number(static_cast<int>(i));
                if (t.lit)
                    std::snprintf(text, sizeof(text), "T%d%s %.0fs", number, t.correct ? " correct" : "", t.lit_left_ms / 1000.0f);
                else
                    std::snprintf(text, sizeof(text), "T%d%s", number, t.correct ? " correct" : "");
                const auto p = screen(nav::project(cam, top), sx, sy);
                const float scale = height / 1080.0f, font_size = std::max(11.0f, 15.0f * scale);
                const auto size = fonts::get(font_size)->CalcTextSizeA(font_size, 10000, 0, text);
                const float y = p.y - size.y - 8 * scale;
                const ImVec2 at(p.x - size.x * 0.5f, y);
                list->AddRectFilled(ImVec2(at.x - 5 * scale, at.y - 2 * scale),
                                    ImVec2(at.x + size.x + 5 * scale, at.y + size.y + 2 * scale), IM_COL32(0, 0, 0, 150), 4 * scale);
                list->AddText(fonts::get(font_size), font_size, ImVec2(at.x + 1, at.y + 1), IM_COL32(0, 0, 0, 220), text);
                list->AddText(fonts::get(font_size), font_size, at, (color & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 255), text);
            }
        }
        const auto& fire = view.fire;
        if (!staff::fresh(fire.updated, now, staff::aim_lifetime_ms)) return;
        const char* names[] = {"Main", "Left", "Right"};
        std::array<std::array<char, 112>, 8> lines{};
        unsigned count = 0;
        auto line = [&](const char* format, auto... values) {
            if (count < lines.size()) std::snprintf(lines[count++].data(), lines[0].size(), format, values...);
        };
        float yaw = 0, pitch = 0;
        staff::angles(fire.forward, yaw, pitch);
        line("Aim          yaw %6.1f   pitch %5.1f", yaw, pitch);
        char label[128] = "Fire:";
        for (unsigned shot = 0; shot < 3; ++shot)
        {
            if (shot > 0 && !fire.sides) continue;
            const int t = fire.torch[shot];
            const bool torch_ok = t >= 0 && staff::fresh(view.torches[static_cast<std::size_t>(t)].updated, now, staff::smoke_lifetime_ms);
            ImU32 color = IM_COL32(170, 170, 170, 255);
            const char* effect = "nothing";
            if (torch_ok)
            {
                const auto& torch = view.torches[static_cast<std::size_t>(t)];
                if (torch.rearm_ms > 0)
                {
                    color = IM_COL32(255, 215, 60, 255);
                    effect = "ignored";
                }
                else if (!torch.correct)
                {
                    color = IM_COL32(255, 70, 70, 255);
                    effect = "WRONG";
                }
                else
                {
                    color = IM_COL32(60, 230, 110, 255);
                    effect = torch.lit ? "relights" : "lights";
                }
            }
            const auto at = nav::camera_space(cam, fire.points[shot]);
            if (nav::point_visible(cam, at))
            {
                const auto p = screen(nav::project(cam, at), sx, sy);
                const float r = shot == 0 ? 14.0f : 10.0f;
                list->AddCircle(p, r, IM_COL32(0, 0, 0, 255), 0, 5);
                list->AddCircle(p, r, color, 0, 3);
            }
            const auto d = fire.points[shot] - fire.muzzle;
            const float distance = std::sqrt(nav::dot(d, d));
            char part[40]{};
            if (torch_ok)
                std::snprintf(part, sizeof(part), " %s T%d %s", names[shot], torch_number(t), effect);
            else
                std::snprintf(part, sizeof(part), " %s -", names[shot]);
            strncat_s(label, part, _TRUNCATE);
            // Nearest torch distance against the 64-unit radius, for every shot.
            float nearest = 1e9f;
            int nearest_index = -1;
            for (std::size_t i = 0; i < view.torches.size(); ++i)
            {
                if (!staff::fresh(view.torches[i].updated, now, staff::smoke_lifetime_ms)) continue;
                const auto e = fire.points[shot] - view.torches[i].origin;
                const float dist = std::sqrt(nav::dot(e, e));
                if (dist < nearest)
                {
                    nearest = dist;
                    nearest_index = static_cast<int>(i);
                }
            }
            const char* side = shot == 0 ? "  0 deg" : shot == 1 ? "-29 deg" : "+29 deg";
            if (nearest_index < 0)
                line("%-5s %s   range %5.0f%s", names[shot], side, distance, distance > 350 ? "   beyond measured range" : "");
            else
                line("%-5s %s   range %5.0f   nearest T%d at %5.1f (hit <= %.0f)%s", names[shot], side, distance,
                     torch_number(nearest_index), nearest, staff::torch_radius, distance > 350 ? "   beyond measured range" : "");
        }
        if (!fire.sides)
            line("Side shots: none (something within 100 units ahead)");
        else
            line("Side shots fire 0.2 s and 0.4 s later, from your aim at that moment");
        if (torches) line("Correct torches lit %u/%u   wrong lit %u", correct_lit, correct, wrong_lit);
        if (!details) return;
        const float scale = height / 1080.0f, font_size = 19.5f * scale;
        const auto size = fonts::get(font_size)->CalcTextSizeA(font_size, 10000, 0, label);
        const ImVec2 pos(width * 0.5f - size.x * 0.5f, height * 0.5f + 60.0f * scale);
        list->AddText(fonts::get(font_size), font_size, ImVec2(pos.x + scale, pos.y + scale), IM_COL32(0, 0, 0, 200), label);
        list->AddText(fonts::get(font_size), font_size, pos, IM_COL32(240, 240, 240, 255), label);
        const float detail_size = 14.25f * scale, step = detail_size + 3 * scale;
        float block = 0;
        for (unsigned i = 0; i < count; ++i)
            block = std::max(block, fonts::get(detail_size)->CalcTextSizeA(detail_size, 10000, 0, lines[i].data()).x);
        const ImVec2 top(width * 0.5f - block * 0.5f, pos.y + font_size + 8 * scale);
        list->AddRectFilled(ImVec2(top.x - 8 * scale, top.y - 4 * scale),
                            ImVec2(top.x + block + 8 * scale, top.y + count * step + 2 * scale), IM_COL32(0, 0, 0, 140), 4 * scale);
        for (unsigned i = 0; i < count; ++i)
            list->AddText(fonts::get(detail_size), detail_size, ImVec2(top.x, top.y + i * step), IM_COL32(230, 232, 236, 255),
                          lines[i].data());
    }

    // Mob of the Dead blue-skull aid (mob_aid.hpp): the 100-unit catch sphere of each
    // skull (green when the predicted check catches it, cyan in its 1.6 s reveal), the
    // skull's bounds (the lightning-hands target, approximate), the predicted tomahawk arc
    // and check ring while the tomahawk is owned, the last real flight and check for a
    // few seconds, and in Afterlife the shock boxes with where to stand to reach them.
    void draw_mob(ImDrawList* list, const nav::Camera& cam, const mob::View& view, float height, float sx, float sy)
    {
        const auto now = GetTickCount64();
        const float scale = height / 1080.0f, font_size = std::max(11.0f, 15.0f * scale);
        auto segment = [&](nav::Vec3 a, nav::Vec3 b, ImU32 color, float thickness) {
            auto p = nav::camera_space(cam, a), q = nav::camera_space(cam, b);
            if (nav::clip_segment(cam, p, q))
                list->AddLine(screen(nav::project(cam, p), sx, sy), screen(nav::project(cam, q), sx, sy), color, thickness);
        };
        auto box = [&](nav::Vec3 lo, nav::Vec3 hi, ImU32 color) {
            const nav::Vec3 c[] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
                                   {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
            const unsigned edges[][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
            for (const auto& e : edges)
                segment(c[e[0]], c[e[1]], color, 1.5f);
        };
        // Hollow ring on screen around a world point, black underlay for contrast.
        auto ring = [&](nav::Vec3 world, float radius, ImU32 color) {
            const auto at = nav::camera_space(cam, world);
            if (!nav::point_visible(cam, at)) return;
            const auto p = screen(nav::project(cam, at), sx, sy);
            list->AddCircle(p, radius, IM_COL32(0, 0, 0, 255), 0, 5);
            list->AddCircle(p, radius, color, 0, 3);
        };
        // Text centred above a screen y (or above the point), on a dark plate.
        auto caption = [&](nav::Vec3 world, float top, ImU32 color, const char* text) {
            const auto at = nav::camera_space(cam, world);
            if (!nav::point_visible(cam, at)) return;
            const auto p = screen(nav::project(cam, at), sx, sy);
            const auto size = fonts::get(font_size)->CalcTextSizeA(font_size, 10000, 0, text);
            float y = top >= 0 ? top - size.y - 8 * scale : p.y - size.y - 12 * scale;
            if (y < 4) y = p.y - size.y - 12 * scale;
            const ImVec2 pos(p.x - size.x * 0.5f, y);
            list->AddRectFilled(ImVec2(pos.x - 5 * scale, pos.y - 2 * scale),
                                ImVec2(pos.x + size.x + 5 * scale, pos.y + size.y + 2 * scale), IM_COL32(0, 0, 0, 150), 4 * scale);
            list->AddText(fonts::get(font_size), font_size, ImVec2(pos.x + 1, pos.y + 1), IM_COL32(0, 0, 0, 220), text);
            list->AddText(fonts::get(font_size), font_size, pos, color, text);
        };
        const bool live = mob::fresh(view.state.updated, now, mob::live_ms);
        if (settings.mob_skulls)
        {
            const bool aiming = live && view.state.tomahawk && !view.state.afterlife && mob::fresh(view.aim.updated, now, mob::live_ms);
            const bool flying = mob::fresh(view.flight.updated, now, mob::flight_ms);
            for (std::size_t i = 0; i < view.skulls.size(); ++i)
            {
                const auto& s = view.skulls[i];
                if (!mob::fresh(s.updated, now, mob::skull_ms)) continue;
                const bool catching = aiming && view.aim.skull == static_cast<int>(i);
                const bool shown = s.shown_ms >= 0;
                const ImU32 color = catching ? IM_COL32(60, 230, 110, 255)
                                    : shown  ? IM_COL32(110, 235, 255, 255)
                                             : IM_COL32(80, 140, 255, 255);
                const float top = draw_sphere(list, cam, s.origin, mob::catch_radius, color, catching || shown, sx, sy);
                box(s.mins, s.maxs, IM_COL32(150, 210, 255, 220));
                char text[48]{};
                if (shown)
                    std::snprintf(text, sizeof(text), "Skull %u  shown %.1fs", static_cast<unsigned>(i + 1), s.shown_ms / 1000.0f);
                else
                    std::snprintf(text, sizeof(text), "Skull %u", static_cast<unsigned>(i + 1));
                caption(s.origin, top, color, text);
            }
            if (aiming)
            {
                const auto& a = view.aim;
                const ImU32 color = a.skull >= 0 ? IM_COL32(60, 230, 110, 230) : IM_COL32(235, 235, 240, 170);
                constexpr int steps = 22;
                for (int k = 0; k < steps; ++k)
                    segment(mob::arc(a, a.stop * k / steps), mob::arc(a, a.stop * (k + 1) / steps), color, 2.0f);
                const ImU32 ring_color = a.skull >= 0 ? IM_COL32(60, 230, 110, 255)
                                         : a.impact   ? IM_COL32(255, 160, 40, 255)
                                                      : IM_COL32(170, 170, 170, 255);
                ring(a.check, 14, ring_color);
                // 3D distance from the check point to the nearest skull centre against the catch
                // radius: on screen a point can sit inside a skull's sphere while being far in
                // front of or behind it (user report 2026-10-14: orange ring inside a blue orb).
                float nearest = -1;
                for (const auto& s : view.skulls)
                {
                    if (!mob::fresh(s.updated, now, mob::skull_ms)) continue;
                    const auto d = s.origin - a.check;
                    const float length = std::sqrt(nav::dot(d, d));
                    if (nearest < 0 || length < nearest) nearest = length;
                }
                if (nearest >= 0)
                {
                    char text[48]{};
                    std::snprintf(text, sizeof(text), "%s%.0f / %.0f", a.impact ? "wall " : "", nearest, mob::catch_radius);
                    caption(a.check, -1, ring_color, text);
                }
            }
            if (flying)
            {
                const auto& f = view.flight;
                for (unsigned k = 1; k < f.count; ++k)
                    segment(f.points[k - 1], f.points[k], IM_COL32(255, 215, 60, 220), 2.0f);
                if (f.done)
                {
                    const ImU32 color = f.skull >= 0 ? IM_COL32(60, 230, 110, 255) : IM_COL32(255, 70, 70, 255);
                    ring(f.check, 10, color);
                    char text[48]{};
                    if (f.skull >= 0)
                        std::snprintf(text, sizeof(text), "Caught skull %d", f.skull + 1);
                    else
                        std::snprintf(text, sizeof(text), "Checked here: no skull");
                    caption(f.check, -1, color, text);
                }
            }
        }
        if (settings.mob_afterlife_spawns)
        {
            // Afterlife ghost spawn spots (mob_aid.hpp). Role by the helper's prediction: zone rule or
            // locked spot (one spot), or the free spots of a group when a random one is still to be picked.
            const auto& next = view.next;
            const bool next_live = mob::fresh(next.updated, now, mob::next_ms);
            for (std::size_t i = 0; i < view.spawns.size(); ++i)
            {
                const auto& s = view.spawns[i];
                if (!mob::fresh(s.updated, now, mob::spawn_ms)) continue;
                const bool chosen = next_live && (next.kind == 1 || next.kind == 2) && next.index == static_cast<int>(i);
                const bool candidate =
                    next_live && next.kind == 3 && next.group == s.group && !(s.state & (mob::spawn_taken | mob::spawn_blocked));
                const bool mine = s.state & mob::spawn_locked;
                if (settings.mob_afterlife_only && !chosen && !candidate) continue;
                if (!chosen && !candidate)
                {
                    const auto d = view.state.player - s.origin;
                    if (nav::dot(d, d) > settings.mob_afterlife_range * settings.mob_afterlife_range) continue;
                }
                ImU32 color = IM_COL32(110, 150, 230, 200);
                const char* role = "";
                if (chosen && next.kind == 1)
                {
                    color = IM_COL32(70, 235, 120, 255);
                    role = "  spawn here (zone rule)";
                }
                else if (chosen)
                {
                    color = IM_COL32(255, 205, 60, 255);
                    role = "  spawn here (locked)";
                }
                else if (candidate)
                {
                    color = IM_COL32(255, 150, 50, 255);
                    role = "  random pick";
                }
                else if (mine)
                {
                    color = IM_COL32(220, 180, 70, 200);
                    role = "  locked to you";
                }
                const bool strong = chosen || candidate;
                const auto at = nav::camera_space(cam, s.origin);
                if (!nav::point_visible(cam, at)) continue;
                const auto p = screen(nav::project(cam, at), sx, sy);
                const float r = (strong ? 9.0f : 6.0f) * std::max(0.8f, scale);
                const ImVec2 top(p.x, p.y - r), east(p.x + r, p.y), bottom(p.x, p.y + r), west(p.x - r, p.y);
                list->AddQuad(top, east, bottom, west, IM_COL32(0, 0, 0, 255), 3);
                list->AddQuadFilled(top, east, bottom, west, color);
                char text[64]{};
                std::snprintf(text, sizeof(text), "Group %d%s", s.group + 1, role);
                list->AddText(fonts::get(font_size), font_size, ImVec2(p.x - r + 1, p.y + r + 3), IM_COL32(0, 0, 0, 200), text);
                list->AddText(fonts::get(font_size), font_size, ImVec2(p.x - r, p.y + r + 2), color, text);
            }
        }
        if (settings.mob_shock_boxes && live && view.state.afterlife)
        {
            const auto player = view.state.player;
            // Declutter (user screenshot 2026-10-14): outlines only for boxes within 1000 units,
            // and the range shape only for the nearest box once within 600.
            constexpr float outline_distance = 1000, range_distance = 600;
            const mob::Box* nearest = nullptr;
            float nearest_distance = 0;
            for (const auto& b : view.boxes)
            {
                if (!mob::fresh(b.updated, now, mob::box_ms)) continue;
                const auto d = player - b.origin;
                const float distance = std::sqrt(nav::dot(d, d));
                if (!nearest || distance < nearest_distance)
                {
                    nearest = &b;
                    nearest_distance = distance;
                }
            }
            for (const auto& b : view.boxes)
            {
                if (!mob::fresh(b.updated, now, mob::box_ms)) continue;
                const auto to = player - b.origin;
                if (nav::dot(to, to) > outline_distance * outline_distance) continue;
                box(b.mins, b.maxs, &b == nearest ? IM_COL32(255, 200, 80, 230) : IM_COL32(255, 200, 80, 110));
                if (&b != nearest || nearest_distance > range_distance) continue;
                // The activation range, static around the box (user request 2026-10-14: the
                // earlier ring followed the player's height). Door boxes test distance2d, an
                // infinite column: a hollow cylinder, drawn 128 above and below the box. The other
                // boxes test 3D distance: a wire sphere (horizontal and two vertical great circles).
                const auto d = player - b.origin;
                const float reach = mob::interact_range;
                const bool inside = b.planar ? d.x * d.x + d.y * d.y < reach * reach : nav::dot(d, d) < reach * reach;
                const ImU32 color = inside ? IM_COL32(60, 230, 110, 200) : IM_COL32(200, 200, 210, 120);
                constexpr int segments = 48;
                auto circle = [&](auto point) {
                    for (int k = 0; k < segments; ++k)
                        segment(point(6.2831853f * k / segments), point(6.2831853f * (k + 1) / segments), color, 1.5f);
                };
                const auto o = b.origin;
                if (b.planar)
                {
                    constexpr float half_height = 128;
                    for (const float z : {o.z - half_height, o.z + half_height})
                        circle([&](float a) { return nav::Vec3{o.x + reach * std::cos(a), o.y + reach * std::sin(a), z}; });
                    for (int k = 0; k < 8; ++k)
                    {
                        const float a = 6.2831853f * k / 8;
                        segment(nav::Vec3{o.x + reach * std::cos(a), o.y + reach * std::sin(a), o.z - half_height},
                                nav::Vec3{o.x + reach * std::cos(a), o.y + reach * std::sin(a), o.z + half_height}, color, 1.5f);
                    }
                }
                else
                {
                    circle([&](float a) { return nav::Vec3{o.x + reach * std::cos(a), o.y + reach * std::sin(a), o.z}; });
                    circle([&](float a) { return nav::Vec3{o.x + reach * std::cos(a), o.y, o.z + reach * std::sin(a)}; });
                    circle([&](float a) { return nav::Vec3{o.x, o.y + reach * std::cos(a), o.z + reach * std::sin(a)}; });
                }
            }
        }
    }

    // World part, under the weapon and HUD: his route (the engine's own, checked node by node
    // against the NAV graph), the 12 phase dashes (yellow fits, red cross does not, bright once
    // the 2 s cooldown is over), the bolt wedge, the bus opening he runs for, and while he is in
    // the storm the spots he can land on.
    // Thrown-grenade aid (grenade_aid.hpp): the predicted path of the frag, EMP or G-strike held
    // back, else of the one carried that the priority option prefers, with its bounces and where
    // it rests or bursts, plus what it reaches there (EMP radii, the five G-strike shells and their
    // 200-unit kill radius, gray for a dud). No text: the rings and circles say it. The last real
    // throw stays drawn for a few seconds, with the prediction made just before it in gray, so a
    // wrong throw model shows at once.
    void draw_grenades(ImDrawList* list, const nav::Camera& cam, const grenade::View& view, float sx, float sy)
    {
        const auto now = GetTickCount64();
        auto segment = [&](nav::Vec3 a, nav::Vec3 b, ImU32 color, float thickness) {
            auto p = nav::camera_space(cam, a), q = nav::camera_space(cam, b);
            if (nav::clip_segment(cam, p, q))
                list->AddLine(screen(nav::project(cam, p), sx, sy), screen(nav::project(cam, q), sx, sy), color, thickness);
        };
        // Level circle in the world (a blast radius on the ground).
        auto circle = [&](nav::Vec3 centre, float radius, ImU32 color, float thickness) {
            constexpr int segments = 48;
            nav::Vec3 previous = centre + nav::Vec3{radius, 0, 0};
            for (int s = 1; s <= segments; ++s)
            {
                const float a = 6.2831853f * s / segments;
                const nav::Vec3 next = centre + nav::Vec3{radius * std::cos(a), radius * std::sin(a), 0};
                segment(previous, next, color, thickness);
                previous = next;
            }
        };
        auto ring = [&](nav::Vec3 world, float radius, ImU32 color) {
            const auto at = nav::camera_space(cam, world);
            if (!nav::point_visible(cam, at)) return;
            const auto p = screen(nav::project(cam, at), sx, sy);
            list->AddCircle(p, radius, IM_COL32(0, 0, 0, 255), 0, 5);
            list->AddCircle(p, radius, color, 0, 3);
        };
        // The path, its bounces and its end. Returns the end point (the last point).
        auto path = [&](const grenade::Path& p, ImU32 color, float thickness, bool marks) {
            for (unsigned k = 1; k < p.count; ++k)
                segment(p.points[k - 1], p.points[k], color, thickness);
            if (marks)
                for (unsigned k = 1; k < p.count; ++k)
                    if (p.flags[k] & grenade::bounce) ring(p.points[k], 5, color);
            return p.points[p.count - 1];
        };
        // What the grenade reaches where it ends: EMP radii, G-strike shells. The shells are kept
        // faint (muted red, gray for a dud) so they do not pull the eye from the throw itself.
        auto reach = [&](const grenade::Path& p, nav::Vec3 end, bool dud) {
            if (p.kind == grenade::emp)
            {
                if (view.emp_outer > 0) circle(end, view.emp_outer, IM_COL32(110, 200, 255, 200), 2.0f);
                if (view.emp_inner > 0) circle(end, view.emp_inner, IM_COL32(255, 220, 90, 200), 2.0f);
            }
            if (p.kind != grenade::beacon) return;
            const ImU32 shells = dud ? IM_COL32(140, 140, 140, 70) : IM_COL32(170, 85, 75, 85);
            for (unsigned i = 0; i < p.strikes; ++i)
            {
                circle(p.strike[i], grenade::strike_radius, shells, 1.0f);
                const auto at = nav::camera_space(cam, p.strike[i]);
                if (nav::point_visible(cam, at)) list->AddCircleFilled(screen(nav::project(cam, at), sx, sy), 2.5f, shells);
            }
        };

        if (const auto* chosen = grenade::pick(view, now, settings.grenade_tactical_first))
        {
            const auto& aim = *chosen;
            const ImU32 color = aim.kind == grenade::frag  ? IM_COL32(255, 170, 60, 230)
                                : aim.kind == grenade::emp ? IM_COL32(110, 200, 255, 230)
                                                           : IM_COL32(255, 110, 80, 230);
            const auto end = path(aim, color, 2.0f, true);
            const bool burst = aim.flags[aim.count - 1] & grenade::burst;
            const bool dud = aim.kind == grenade::beacon && aim.verdict == 0;
            reach(aim, end, dud);
            ring(end, 12, burst ? IM_COL32(255, 70, 70, 255) : color);
        }

        const auto& f = view.flight;
        if (grenade::fresh(f.updated, now, grenade::flight_ms) && f.count)
        {
            if (view.thrown.count) path(view.thrown, IM_COL32(200, 200, 205, 110), 1.5f, false);
            const ImU32 color = IM_COL32(255, 215, 60, 220);
            const auto end = path(f, color, 2.0f, true);
            if (f.ended)
            {
                const bool dud = f.kind == grenade::beacon && f.verdict == 0;
                reach(f, end, dud);
                const bool burst = f.flags[f.count - 1] & grenade::burst;
                ring(end, 10, burst ? IM_COL32(255, 70, 70, 255) : color);
            }
        }
    }

    // Origins giant robot footsteps (robot_aid.hpp), for the active robot only (robot::active:
    // the walking robot with the hatch). Every big step still ahead on its route gets an orb
    // fixed where that step will land: a bright ring where the clue sphere around the foot's stomp
    // trigger meets the player's height (the game's test is 3D, so standing on the ring is exactly
    // the limit), under a low faint dome of the sphere above it, nothing below. Green ring: a step there would send the
    // clue now. The next step is drawn brighter. At each step's check a red shockwave runs out to
    // the ring and fades. The spots come from the recorded routes (robot_routes.hpp). Steps whose
    // sphere holds no walkable ground (no path node inside) are never drawn: no player can be
    // in them. The intro walk has no orbs (at game start no clue can play). A dot marks where the
    // player stood at each recent check, green when the game sent the clue, red when not. Gold
    // diamond: the hatch foot, filled while its hatch is open.
    void draw_robot_world(ImDrawList* list, const nav::Camera& cam, const robot::View& view, float sx, float sy)
    {
        const auto now = GetTickCount64();
        const bool live = robot::fresh(view.player.updated, now, robot::live_ms);
        const ImU32 white = IM_COL32(225, 225, 235, 255), green = IM_COL32(60, 230, 110, 255), red = IM_COL32(255, 60, 60, 255);
        auto alpha = [](ImU32 color, int a) { return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT); };
        auto segment = [&](nav::Vec3 a, nav::Vec3 b, ImU32 color, float thickness) {
            auto p = nav::camera_space(cam, a), q = nav::camera_space(cam, b);
            if (nav::clip_segment(cam, p, q))
                list->AddLine(screen(nav::project(cam, p), sx, sy), screen(nav::project(cam, q), sx, sy), color, thickness);
        };
        // Level circle of radius r at height z around centre.
        auto circle = [&](nav::Vec3 centre, float r, float z, ImU32 color, float thickness, int segments) {
            if (r <= 0) return;
            nav::Vec3 previous{centre.x + r, centre.y, z};
            for (int s = 1; s <= segments; ++s)
            {
                const float a = 6.2831853f * s / segments;
                const nav::Vec3 next{centre.x + r * std::cos(a), centre.y + r * std::sin(a), z};
                segment(previous, next, color, thickness);
                previous = next;
            }
        };
        // The sphere's cross-section at height z.
        auto section = [&](nav::Vec3 centre, float z, ImU32 color, float thickness, int segments = 96) {
            circle(centre, robot::section_radius(robot::clue_radius, z - centre.z), z, color, thickness, segments);
        };
        // A low dome over the ground ring (user request 2026-11-20: nothing underground, not the
        // whole sphere): the sphere's surface from the ring's height up robot::dome_height, as
        // level circles and meridian arcs.
        auto dome = [&](nav::Vec3 c, float base, ImU32 color, int levels, int meridians) {
            const float top = std::min(base + robot::dome_height, c.z + robot::clue_radius);
            if (top <= base) return;
            for (int k = 1; k <= levels; ++k)
                section(c, base + (top - base) * k / levels, color, 1.0f, 48);
            constexpr int arc = 8;
            for (int m = 0; m < meridians; ++m)
            {
                const float yaw = 6.2831853f * m / meridians, cx = std::cos(yaw), cy = std::sin(yaw);
                auto point = [&](int k) {
                    const float z = base + (top - base) * k / arc, r = std::max(0.0f, robot::section_radius(robot::clue_radius, z - c.z));
                    return nav::Vec3{c.x + r * cx, c.y + r * cy, z};
                };
                for (int k = 0; k < arc; ++k)
                    segment(point(k), point(k + 1), color, 1.0f);
            }
        };
        auto at_screen = [&](nav::Vec3 world, ImVec2& out) {
            const auto at = nav::camera_space(cam, world);
            if (!nav::point_visible(cam, at)) return false;
            out = screen(nav::project(cam, at), sx, sy);
            return true;
        };
        auto ring_height = [&](nav::Vec3 c) { return live ? view.player.origin.z : c.z; };
        auto inside = [&](nav::Vec3 c) { return live && robot::counts(view.player, c); };
        // One clue orb at a planned step. strong: the next step.
        auto orb = [&](nav::Vec3 c, bool strong) {
            const ImU32 color = inside(c) ? green : white;
            const float z = ring_height(c), thickness = strong ? 2.5f : 1.5f;
            dome(c, z, alpha(color, strong ? 60 : 30), strong ? 3 : 2, strong ? 24 : 12);
            section(c, z, IM_COL32(0, 0, 0, strong ? 120 : 70), thickness + 2);
            section(c, z, alpha(color, strong ? 220 : 120), thickness);
            if (std::abs(z - c.z) > 8) segment(c, nav::Vec3{c.x, c.y, z}, alpha(color, strong ? 160 : 80), 1.0f);
            ImVec2 p;
            if (!at_screen(c, p)) return;
            list->AddCircleFilled(p, strong ? 6.0f : 4.0f, IM_COL32(0, 0, 0, 200));
            list->AddCircleFilled(p, strong ? 4.5f : 3.0f, alpha(color, strong ? 255 : 170));
        };
        // The red shockwave of a step at its check: a thick ring runs out to the sphere's edge at
        // the player's height in the first 0.35 s, then the ring and the sphere fade out.
        auto shockwave = [&](nav::Vec3 c, std::uint64_t updated) {
            const float t = static_cast<float>(now - updated) / static_cast<float>(robot::highlight_ms);
            const float edge = robot::section_radius(robot::clue_radius, ring_height(c) - c.z);
            const float grow = std::min(1.0f, static_cast<float>(now - updated) / 350.0f);
            const int a = static_cast<int>(255 * (1 - t));
            const float z = ring_height(c);
            dome(c, z, alpha(red, a / 3), 3, 24);
            if (edge <= 0) return;
            const float r = edge * (1 - (1 - grow) * (1 - grow));
            circle(c, r, z, IM_COL32(0, 0, 0, a * 2 / 3), 7.0f, 96);
            circle(c, r, z, alpha(red, a), 5.0f, 96);
        };

        const auto active = robot::active(view, now);
        for (int id = 0; id < robot::robot_count; ++id)
        {
            if (!active[static_cast<std::size_t>(id)]) continue;
            const auto& r = view.robots[static_cast<std::size_t>(id)];
            const auto steps = robot::planned(r.route);
            bool next = true;
            for (int i = r.checked; i < steps.count; ++i)
            {
                if (!steps.first[i].reaches) continue;
                orb(robot::at(steps.first[i]), next);
                next = false;
            }
            for (int foot = 0; foot < 2; ++foot)
            {
                const auto c = r.feet[static_cast<std::size_t>(foot)];
                const auto& step = view.steps[static_cast<std::size_t>(id)][static_cast<std::size_t>(foot)];
                if (robot::fresh(step.updated, now, robot::highlight_ms) && step.route == r.route && step.index < steps.count &&
                    steps.first[step.index].reaches)
                    shockwave(step.trigger, step.updated);
                // The foot itself, where it is now.
                ImVec2 p;
                if (!at_screen(c, p)) continue;
                list->AddCircle(p, 7, IM_COL32(0, 0, 0, 255), 0, 4);
                list->AddCircle(p, 7, white, 0, 2);
                if (r.hatch != foot) continue;
                const ImVec2 top(p.x, p.y - 26), east(p.x + 8, p.y - 18), bottom(p.x, p.y - 10), west(p.x - 8, p.y - 18);
                const ImU32 gold = IM_COL32(255, 200, 60, 255);
                list->AddQuad(top, east, bottom, west, IM_COL32(0, 0, 0, 255), 3.0f);
                if (r.hatch_open)
                    list->AddQuadFilled(top, east, bottom, west, gold);
                else
                    list->AddQuad(top, east, bottom, west, gold, 1.5f);
            }
            // Where the player stood at this robot's recent checks.
            for (const auto& step : view.steps[static_cast<std::size_t>(id)])
            {
                if (!robot::fresh(step.updated, now, robot::step_ms)) continue;
                ImVec2 p;
                if (!at_screen(step.player, p)) continue;
                list->AddCircleFilled(p, 5, IM_COL32(0, 0, 0, 230));
                list->AddCircleFilled(p, 3.5f, alpha(step.notified ? green : red, 230));
            }
        }
    }

    // Robot footstep readout at the top of the screen, for the active robot: distance to its
    // nearest foot, its route (step count, how far the last step landed from its recorded spot,
    // the live check of robot_routes.hpp), the last step's distance and whether the game sent the clue, and the
    // clue's 10 s wait before it checks for zombie blood (the helper's copy of that wait).
    void draw_robot_text(ImDrawList* list, const robot::View& view, float width, float height)
    {
        const auto now = GetTickCount64();
        const auto& player = view.player;
        if (!robot::fresh(player.updated, now, robot::live_ms)) return;
        // Robot ids by the route they walk (zm_tomb_giant_robot: 0 shakes the Pack-a-Punch, 1 the
        // tank bunker, 2 the church ceiling).
        static constexpr const char* names[robot::robot_count] = {"NML", "Trenches", "Church"};
        static constexpr const char* routes[robot::route_count] = {"NML", "Trenches", "Church", "Intro"};
        static constexpr const char* feet[2] = {"left", "right"};

        struct Line
        {
            std::array<char, 128> text{};
            ImU32 color = 0;
        };

        std::array<Line, 4> lines{};
        unsigned count = 0;
        auto line = [&](ImU32 color, const char* format, auto... values) {
            if (count >= lines.size()) return;
            std::snprintf(lines[count].text.data(), lines[count].text.size(), format, values...);
            lines[count++].color = color;
        };
        const ImU32 green = IM_COL32(60, 230, 110, 255), red = IM_COL32(255, 90, 90, 255), plain = IM_COL32(230, 232, 236, 255);

        const auto active = robot::active(view, now);
        int near_id = -1, near_foot = 0;
        float near_distance = 0;
        for (int id = 0; id < robot::robot_count; ++id)
        {
            const auto& r = view.robots[static_cast<std::size_t>(id)];
            if (!active[static_cast<std::size_t>(id)]) continue;
            for (int foot = 0; foot < 2; ++foot)
            {
                const auto d = player.origin - r.feet[static_cast<std::size_t>(foot)];
                const float distance = std::sqrt(nav::dot(d, d));
                if (near_id < 0 || distance < near_distance)
                {
                    near_id = id;
                    near_foot = foot;
                    near_distance = distance;
                }
            }
        }
        if (near_id >= 0)
        {
            if (player.head >= 0)
                line(red, "In the %s robot's head: steps send no clue", names[player.head]);
            else if (!player.valid)
                line(red, "Down: steps send no clue");
            else
                line(near_distance < robot::clue_radius ? green : plain, "Nearest foot %.0f units (%s %s), a step counts under %.0f",
                     near_distance, names[near_id], feet[near_foot], robot::clue_radius);
            const auto& r = view.robots[static_cast<std::size_t>(near_id)];
            const auto steps = robot::planned(r.route);
            const auto& recent = view.steps[static_cast<std::size_t>(near_id)];
            const auto& latest = recent[0].updated > recent[1].updated ? recent[0] : recent[1];
            const float off =
                latest.route == r.route && latest.index + 1 == r.checked ? robot::off_table(r.route, latest.index, latest.trigger) : -1;
            if (!steps.count)
                line(plain, "%s walk: no recorded steps (no clue can play yet)", routes[r.route]);
            else if (off >= 0)
                line(off <= robot::table_tolerance ? plain : red, "%s route: step %d of %d, %.1f units from its recorded spot%s",
                     routes[r.route], r.checked, steps.count, off, off <= robot::table_tolerance ? "" : " (recording out of date)");
            else
                line(plain, "%s route: step %d of %d", routes[r.route], r.checked, steps.count);
        }

        const robot::Step* last = nullptr;
        int last_id = 0, last_foot = 0;
        for (int id = 0; id < robot::robot_count; ++id)
            for (int foot = 0; foot < 2; ++foot)
            {
                const auto& step = view.steps[static_cast<std::size_t>(id)][static_cast<std::size_t>(foot)];
                if (!active[static_cast<std::size_t>(id)]) continue;
                if (!robot::fresh(step.updated, now, robot::step_ms) || (last && step.updated <= last->updated)) continue;
                last = &step;
                last_id = id;
                last_foot = foot;
            }
        if (last)
            line(last->notified ? green : red, "Step %s %s: %.0f units, clue %s", names[last_id], feet[last_foot], last->distance,
                 last->notified ? "sent" : "not sent");

        if (player.clue == robot::Clue::waiting)
            line(player.zombie_blood ? green : red, "Wind clue checks zombie blood in %.1f s: zombie blood %s", player.clue_ms / 1000.0f,
                 player.zombie_blood ? "on" : "off");
        if (!count) return;

        const float scale = height / 1080.0f, size = 16.5f * scale, step = size + 4 * scale;
        float block = 0;
        for (unsigned i = 0; i < count; ++i)
            block = std::max(block, fonts::get(size)->CalcTextSizeA(size, 10000, 0, lines[i].text.data()).x);
        const ImVec2 top(width * 0.5f - block * 0.5f, 96 * scale);
        list->AddRectFilled(ImVec2(top.x - 8 * scale, top.y - 4 * scale),
                            ImVec2(top.x + block + 8 * scale, top.y + count * step + 2 * scale), IM_COL32(0, 0, 0, 140), 4 * scale);
        for (unsigned i = 0; i < count; ++i)
        {
            const ImVec2 pos(top.x, top.y + i * step);
            list->AddText(fonts::get(size), size, ImVec2(pos.x + scale, pos.y + scale), IM_COL32(0, 0, 0, 200), lines[i].text.data());
            list->AddText(fonts::get(size), size, pos, lines[i].color, lines[i].text.data());
        }
    }

    void draw_avogadro_world(ImDrawList* list, const nav::Camera& cam, const nav::Graph& g, const avogadro::View& view, float label_size,
                             float sx, float sy)
    {
        const auto now = GetTickCount64();
        const auto& s = view.status;
        if (settings.avogadro_spawns)
        {
            // His own landing spots (avogadro_location), all of them, as electric-blue diamonds.
            for (const auto& spot : view.spawns)
            {
                if (!avogadro::fresh(spot.updated, now, avogadro::spawn_ms)) continue;
                const auto at = nav::camera_space(cam, spot.origin);
                if (!nav::point_visible(cam, at)) continue;
                const auto p = screen(nav::project(cam, at), sx, sy);
                constexpr float r = 7;
                const ImVec2 top(p.x, p.y - r), east(p.x + r, p.y), bottom(p.x, p.y + r), west(p.x - r, p.y);
                list->AddQuad(top, east, bottom, west, IM_COL32(0, 0, 0, 255), 3);
                list->AddQuadFilled(top, east, bottom, west,
                                    spot.candidate && s.in_storm() ? IM_COL32(150, 230, 255, 255) : IM_COL32(90, 200, 255, 235));
                list->AddText(fonts::get(label_size), label_size, ImVec2(p.x - r + 1, p.y + r + 3), IM_COL32(0, 0, 0, 200), "Avogadro");
                list->AddText(fonts::get(label_size), label_size, ImVec2(p.x - r, p.y + r + 2), IM_COL32(150, 215, 255, 255), "Avogadro");
            }
        }
        if (!settings.avogadro_aid || !avogadro::fresh(s.updated, now, avogadro::live_ms)) return;
        auto segment = [&](nav::Vec3 a, nav::Vec3 b, ImU32 color, float thickness) {
            auto p = nav::camera_space(cam, a), q = nav::camera_space(cam, b);
            if (nav::clip_segment(cam, p, q))
                list->AddLine(screen(nav::project(cam, p), sx, sy), screen(nav::project(cam, q), sx, sy), color, thickness);
        };
        auto pixel = [&](nav::Vec3 world, ImVec2& out) {
            const auto at = nav::camera_space(cam, world);
            if (!nav::point_visible(cam, at)) return false;
            out = screen(nav::project(cam, at), sx, sy);
            return true;
        };
        auto label = [&](ImVec2 at, ImU32 color, const char* text) {
            list->AddText(fonts::get(label_size), label_size, ImVec2(at.x + 1, at.y + 1), IM_COL32(0, 0, 0, 200), text);
            list->AddText(fonts::get(label_size), label_size, at, color, text);
        };
        constexpr ImU32 electric = IM_COL32(90, 200, 255, 230), dash = IM_COL32(255, 225, 70, 235), shade = IM_COL32(0, 0, 0, 150);
        const nav::Vec3 lift{0, 0, 6};
        zombies::Marker marker{};
        nav::Vec3 origin{};
        const bool body = avogadro_body(s, g, marker, origin);
        zombies::Route route;
        if (body && settings.avogadro_route && (s.state == avogadro::chasing || s.state == avogadro::chasing_bus) &&
            zombies::route(marker, g, route, origin))
        {
            auto previous = origin;
            for (auto i = zombies::route_ahead(route.points, origin); i < route.points.size(); ++i)
            {
                segment(previous + lift, route.points[i] + lift, shade, 5.0f);
                segment(previous + lift, route.points[i] + lift, electric, 2.5f);
                ImVec2 p{};
                if (pixel(route.points[i] + lift, p)) list->AddCircleFilled(p, 3.0f, electric, 10);
                previous = route.points[i];
            }
            // The far end: the goal itself (filled), or where a long stored route stops (hollow).
            ImVec2 end{};
            if (pixel(route.points.back() + lift, end))
            {
                if (route.complete)
                {
                    list->AddCircleFilled(end, 7, IM_COL32(0, 0, 0, 220), 16);
                    list->AddCircleFilled(end, 5, electric, 16);
                }
                else
                {
                    list->AddCircle(end, 8, IM_COL32(0, 0, 0, 220), 0, 4.5f);
                    list->AddCircle(end, 8, electric, 0, 2.0f);
                }
            }
        }
        if (body && settings.avogadro_dashes && s.state == avogadro::chasing)
        {
            const bool ready = s.phase_ms <= 0 && !s.has(avogadro::traversing);
            const ImU32 fits = ready ? dash : IM_COL32(255, 225, 70, 90),
                        blocked = ready ? IM_COL32(255, 70, 70, 215) : IM_COL32(255, 70, 70, 80);
            for (const auto& d : view.dashes)
            {
                if (!avogadro::fresh(d.updated, now, avogadro::dash_ms)) continue;
                if (d.clear) segment(origin + lift, d.end + lift, fits, ready ? 2.0f : 1.0f);
                ImVec2 p{};
                if (!pixel(d.end + lift, p)) continue;
                if (d.clear)
                {
                    list->AddCircleFilled(p, ready ? 5.5f : 4.0f, IM_COL32(0, 0, 0, ready ? 220 : 110), 12);
                    list->AddCircleFilled(p, ready ? 4.0f : 2.5f, fits, 12);
                }
                else
                {
                    list->AddLine(ImVec2(p.x - 4, p.y - 4), ImVec2(p.x + 4, p.y + 4), blocked, 2);
                    list->AddLine(ImVec2(p.x - 4, p.y + 4), ImVec2(p.x + 4, p.y - 4), blocked, 2);
                }
            }
        }
        if (body && s.state == avogadro::chasing && avogadro::fresh(view.aim.updated, now, avogadro::live_ms))
        {
            // Flat at his feet. Vanilla tests the 3D direction to your origin, so with a height
            // difference the real cone is narrower than the wedge.
            const auto f = view.aim.forward;
            if (f.x * f.x + f.y * f.y > 0.01f)
            {
                const float yaw = std::atan2(f.y, f.x), half = std::acos(avogadro::bolt_dot);
                const bool aimed = s.has(avogadro::bolt_range) && s.has(avogadro::bolt_facing);
                const ImU32 color = aimed && s.has(avogadro::bolt_sight) ? IM_COL32(255, 80, 80, 230)
                                    : aimed                              ? IM_COL32(255, 190, 60, 200)
                                                                         : IM_COL32(205, 205, 220, 110);
                auto at = [&](float angle, float radius) {
                    return nav::Vec3{origin.x + radius * std::cos(angle), origin.y + radius * std::sin(angle), origin.z + lift.z};
                };
                for (const float side : {-half, half})
                    segment(at(yaw + side, avogadro::bolt_min), at(yaw + side, avogadro::bolt_max), color, 1.5f);
                constexpr int steps = 6;
                for (const float radius : {avogadro::bolt_min, avogadro::bolt_max})
                    for (int k = 0; k < steps; ++k)
                        segment(at(yaw - half + 2 * half * static_cast<float>(k) / steps, radius),
                                at(yaw - half + 2 * half * static_cast<float>(k + 1) / steps, radius), color, 1.5f);
            }
        }
        if (body && s.state == avogadro::chasing_bus && avogadro::fresh(view.bus.updated, now, avogadro::live_ms))
        {
            segment(origin + nav::Vec3{0, 0, 40}, view.bus.target, IM_COL32(255, 190, 60, 210), 2.0f);
            ImVec2 p{};
            if (pixel(view.bus.target, p))
            {
                list->AddCircle(p, 8, IM_COL32(0, 0, 0, 220), 0, 4.5f);
                list->AddCircle(p, 8, IM_COL32(255, 190, 60, 255), 0, 2.0f);
            }
        }
        if (s.in_storm())
        {
            for (const auto& spot : view.spawns)
            {
                if (!avogadro::fresh(spot.updated, now, avogadro::spawn_ms) || !spot.candidate) continue;
                const auto top = spot.origin + nav::Vec3{0, 0, 200};
                segment(spot.origin, top, shade, 5.0f);
                segment(spot.origin, top, electric, 2.5f);
                ImVec2 p{};
                if (!pixel(spot.origin, p)) continue;
                list->AddCircle(p, 9, IM_COL32(0, 0, 0, 220), 0, 5);
                list->AddCircle(p, 9, electric, 0, 2.5f);
                label(ImVec2(p.x + 12, p.y - label_size * 0.5f), electric, "Avogadro may land here");
            }
        }
    }

    // Text part, at Present over the weapon and HUD so it stays readable: describe()'s lines on
    // a plate over his head. While he is in the storm, his head is off screen or he is not
    // found, the same lines at the top centre of the screen.
    void draw_avogadro_text(ImDrawList* list, const nav::Camera& cam, const nav::Graph& g, const avogadro::View& view, float width,
                            float height, float sx, float sy)
    {
        const auto now = GetTickCount64();
        const auto& s = view.status;
        const float scale = height / 1080.0f;
        const bool live = avogadro::fresh(s.updated, now, avogadro::live_ms);
        std::vector<avogadro::Line> lines;
        if (!live)
            lines.push_back({"Avogadro aid: no Avogadro in this game (classic TranZit only)", avogadro::Tone::dim});
        else
        {
            int known = 0, fit = 0;
            for (const auto& d : view.dashes)
                if (avogadro::fresh(d.updated, now, avogadro::dash_ms))
                {
                    ++known;
                    if (d.clear) ++fit;
                }
            lines = avogadro::describe(avogadro::with_aim(s, view.aim, now), view.bus,
                                       avogadro::fresh(view.bus.updated, now, avogadro::live_ms), known == avogadro::dash_count ? fit : -1);
        }
        zombies::Marker marker{};
        nav::Vec3 origin{};
        ImVec2 head{};
        bool overhead = false;
        if (live && !settings.avogadro_locked && avogadro_body(s, g, marker, origin))
        {
            const auto to = origin - cam.origin;
            const auto at = nav::camera_space(cam, origin + nav::Vec3{0, 0, 92});
            if (nav::dot(to, to) <= settings.avogadro_range * settings.avogadro_range && nav::point_visible(cam, at))
            {
                head = screen(nav::project(cam, at), sx, sy);
                overhead = true;
            }
        }
        // The top panel is read from the corner of the eye: larger than the label (user request 2026-10-30).
        const float font_size = std::max(11.0f, (overhead ? 16.0f : 21.0f) * scale), pad = (overhead ? 6.0f : 8.0f) * scale,
                    line_height = font_size + 3 * scale;
        auto* font = fonts::get(font_size);
        float plate_width = 0;
        for (const auto& l : lines)
            plate_width = std::max(plate_width, font->CalcTextSizeA(font_size, 10000, 0, l.text.c_str()).x);
        const float plate_height = line_height * static_cast<float>(lines.size());
        ImVec2 top = overhead ? ImVec2(head.x - plate_width * 0.5f, head.y - plate_height - pad)
                              : ImVec2((width - plate_width) * 0.5f, 10 * scale + pad);
        top.x = std::clamp(top.x, pad, std::max(pad, width - plate_width - pad));
        top.y = std::clamp(top.y, pad, std::max(pad, height - plate_height - pad));
        list->AddRectFilled(ImVec2(top.x - pad, top.y - pad * 0.6f), ImVec2(top.x + plate_width + pad, top.y + plate_height + pad * 0.4f),
                            IM_COL32(8, 12, 22, 175), 5 * scale);
        for (std::size_t i = 0; i < lines.size(); ++i)
        {
            const ImVec2 at(top.x, top.y + line_height * static_cast<float>(i));
            list->AddText(font, font_size, ImVec2(at.x + 1, at.y + 1), IM_COL32(0, 0, 0, 220), lines[i].text.c_str());
            list->AddText(font, font_size, at, tone_color(lines[i].tone), lines[i].text.c_str());
        }
    }
} // namespace overlay
