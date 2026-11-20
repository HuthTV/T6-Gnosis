// World drawings: NAV graph, spawn sites, trigger volumes and the playable area, world
// collision, the player box and zombie markers. draw() also dispatches the map aids.
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <future>
#include <string>
#include <vector>
#include "overlay_shared.hpp"
#include "fonts.hpp"
#include "frame_trace.hpp"
#include "playable_area.hpp"
#include "zombies.hpp"

namespace overlay
{
    namespace
    {
        // The player entity's axis-aligned bounds (the bbox option).
        void draw_player_box(ImDrawList* list, const nav::Camera& cam, const triggers::PlayerBox& player, float sx, float sy)
        {
            const auto lo = player.origin + player.mins, hi = player.origin + player.maxs;
            const nav::Vec3 corners[] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
                                         {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
            const unsigned edges[][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
            for (const auto& edge : edges)
            {
                auto a = nav::camera_space(cam, corners[edge[0]]), b = nav::camera_space(cam, corners[edge[1]]);
                if (nav::clip_segment(cam, a, b))
                    list->AddLine(screen(nav::project(cam, a), sx, sy), screen(nav::project(cam, b), sx, sy), IM_COL32(70, 220, 255, 255),
                                  2.0f);
            }
        }

        // World collision as solid faces inside a box around the eye. Painter's algorithm:
        // back faces are culled and the rest drawn far to near, so only the nearest collision
        // surface shows (overlaps are hidden fully at opacity 1). Fill is shaded by how
        // directly a face looks at the eye. Its edges are a darker shade of the same colour.
        void draw_collision(ImDrawList* list, const nav::Camera& cam, const collision::Snapshot& s, float sx, float sy)
        {
            constexpr unsigned budget = 15000;
            const auto c = cam.origin;
            const float h = settings.collision_range, v = settings.collision_range;
            const int x0 = static_cast<int>(std::floor((c.x - h) / collision::cell_size)),
                      x1 = static_cast<int>(std::floor((c.x + h) / collision::cell_size));
            const int y0 = static_cast<int>(std::floor((c.y - h) / collision::cell_size)),
                      y1 = static_cast<int>(std::floor((c.y + h) / collision::cell_size));
            std::vector<std::pair<float, std::uint32_t>> order;
            for (int ix = x0; ix <= x1; ++ix)
                for (int iy = y0; iy <= y1; ++iy)
                {
                    const auto cell =
                        s.cells.find(collision::cell_key((ix + 0.5f) * collision::cell_size, (iy + 0.5f) * collision::cell_size));
                    if (cell == s.cells.end()) continue;
                    for (const auto index : cell->second)
                    {
                        const auto& f = s.faces[index];
                        if (std::abs(f.centre.x - c.x) > h || std::abs(f.centre.y - c.y) > h || std::abs(f.centre.z - c.z) > v) continue;
                        const auto to = f.centre - c;
                        if (nav::dot(f.normal, to) >= 0) continue; // back face: the far side of a brush or a downward-facing triangle
                        order.push_back({nav::dot(to, cam.forward), index});
                    }
                }
            std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            if (order.size() > budget) order.erase(order.begin(), order.end() - budget); // keep the nearest
            const auto alpha = static_cast<unsigned>(std::clamp(settings.collision_opacity, 0.0f, 1.0f) * 255.0f + 0.5f);
            std::vector<ImVec2> pixels;
            for (const auto& [depth, index] : order)
            {
                const auto& f = s.faces[index];
                std::vector<nav::Vec3> points;
                points.reserve(f.points.size());
                for (const auto p : f.points)
                    points.push_back(nav::camera_space(cam, p));
                pixels.clear();
                for (const auto p : nav::clip_polygon(cam, points))
                    pixels.push_back(screen(nav::project(cam, p), sx, sy));
                if (pixels.size() < 3) continue;
                float r = 80, g = 200, b = 255;
                switch (f.kind)
                {
                case collision::Kind::floor:
                    r = 80;
                    g = 230;
                    b = 120;
                    break;
                case collision::Kind::roof:
                    r = 255;
                    g = 160;
                    b = 60;
                    break;
                case collision::Kind::clip:
                    r = 255;
                    g = 80;
                    b = 255;
                    break;
                default: break;
                }
                const auto to = f.centre - c;
                const float distance = std::sqrt(nav::dot(to, to));
                const float facing = distance > 1 ? -nav::dot(f.normal, to) / distance : 1;
                const float shade = 0.45f + 0.55f * std::clamp(facing, 0.0f, 1.0f);
                const auto fill = IM_COL32(static_cast<int>(r * shade), static_cast<int>(g * shade), static_cast<int>(b * shade), alpha);
                const auto edge = IM_COL32(static_cast<int>(r * shade * 0.45f), static_cast<int>(g * shade * 0.45f),
                                           static_cast<int>(b * shade * 0.45f), std::min(255u, alpha + 40u));
                const int count = static_cast<int>(pixels.size());
                list->AddConvexPolyFilled(pixels.data(), count, fill);
                list->AddPolyline(pixels.data(), count, edge, ImDrawFlags_Closed, 1.0f);
            }
        }

        // Playable area: the merged surface of every enabled and life volume on the map
        // (playable_area.hpp). The trigger snapshot is replaced twice a second, so the volumes
        // are fingerprinted and the surface is rebuilt only when they change (a zone opening,
        // a new map). The build runs on a worker thread. Until it finishes, the previous
        // surface stays drawn (none after a map change), so a rebuild never stalls a frame
        // (user report 2026-10-20: rebuilding on every snapshot made the game lag).
        struct AreaSurface
        {
            std::uint64_t fingerprint = 0;
            std::vector<area::Piece> pieces;
            std::vector<area::Edge> edges;
        };

        struct AreaState
        {
            std::shared_ptr<const triggers::Snapshot> source;
            std::shared_ptr<const nav::Graph> map;
            AreaSurface surface;
            std::future<AreaSurface> pending;
            std::uint64_t pending_fingerprint = 0;
        } area_state;

        const AreaSurface& playable_surface(const std::shared_ptr<const triggers::Snapshot>& snapshot)
        {
            auto& a = area_state;
            if (a.pending.valid() && a.pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                auto built = a.pending.get();
                if (built.fingerprint == a.pending_fingerprint) a.surface = std::move(built);
            }
            if (a.source == snapshot) return a.surface;
            a.source = snapshot;
            if (!a.map || !snapshot->map || !spawns::same_map(*a.map, *snapshot->map))
            {
                a.map = snapshot->map;
                a.surface = {};
                a.pending_fingerprint = 0;
            } // a running build for the old map is dropped
            std::vector<area::Hull> hulls;
            std::uint64_t fingerprint = 1469598103934665603ull; // FNV-1a
            auto mix = [&](std::int64_t value) {
                for (int i = 0; i < 8; ++i)
                {
                    fingerprint ^= static_cast<std::uint64_t>(value >> (i * 8)) & 0xFF;
                    fingerprint *= 1099511628211ull;
                }
            };
            for (const auto& marker : snapshot->markers)
            {
                const auto role = triggers::region(marker.fields);
                if (role != triggers::Region::enabled_player && role != triggers::Region::life) continue;
                nav::Vec3 origin{}, angles{};
                if (!triggers::current_pose(marker, origin, angles)) continue;
                for (const auto& mesh : marker.meshes)
                {
                    std::vector<std::vector<nav::Vec3>> faces;
                    for (const auto& face : mesh.faces)
                    {
                        std::vector<nav::Vec3> points;
                        for (const auto v : face)
                        {
                            const auto p = origin + (marker.shape == "Cylinder" ? v : zombies::rotate(v, angles));
                            points.push_back(p);
                            mix(std::lround(p.x * 4));
                            mix(std::lround(p.y * 4));
                            mix(std::lround(p.z * 4));
                        }
                        faces.push_back(std::move(points));
                    }
                    mix(role == triggers::Region::life ? 2 : 1);
                    area::Hull hull;
                    if (area::make_hull(std::move(faces), role == triggers::Region::life, hull)) hulls.push_back(std::move(hull));
                }
            }
            if (fingerprint == a.surface.fingerprint || (a.pending.valid() && fingerprint == a.pending_fingerprint)) return a.surface;
            // One build at a time: a newer change is picked up by a later snapshot once it ends.
            if (a.pending.valid()) return a.surface;
            a.pending_fingerprint = fingerprint;
            a.pending = std::async(std::launch::async, [hulls = std::move(hulls), fingerprint] {
                AreaSurface built;
                built.fingerprint = fingerprint;
                area::build_surface(hulls, built.pieces, built.edges);
                return built;
            });
            return a.surface;
        }
    } // namespace

    // GPU path for the collision view: the same faces as draw_collision, turned into
    // clip-space vertices for collision_gpu (exact per-pixel occlusion, no stacking).
    // Triangles are two-sided: their winding is not reliable, so each is oriented toward
    // the eye and classified from there (horizontal below you = floor, above = roof).
    // Brush faces keep their outward normals and are culled when facing away.
    void build_collision(const nav::Camera& cam, const collision::Snapshot& s, const std::vector<collision::Face>& moving, float sx,
                         float sy, collision_gpu::Batch& batch)
    {
        constexpr unsigned budget = 40000;
        batch.viewport_x = cam.x * sx;
        batch.viewport_y = cam.y * sy;
        batch.viewport_width = cam.width * sx;
        batch.viewport_height = cam.height * sy;
        const auto c = cam.origin;
        const float h = settings.collision_range, v = settings.collision_range;
        const int x0 = static_cast<int>(std::floor((c.x - h) / collision::cell_size)),
                  x1 = static_cast<int>(std::floor((c.x + h) / collision::cell_size));
        const int y0 = static_cast<int>(std::floor((c.y - h) / collision::cell_size)),
                  y1 = static_cast<int>(std::floor((c.y + h) / collision::cell_size));
        // Candidates: faces listed in the cells under the range box, plus the very large ones.
        std::vector<std::uint32_t> candidates(s.large);
        for (int ix = x0; ix <= x1; ++ix)
            for (int iy = y0; iy <= y1; ++iy)
            {
                const auto cell = s.cells.find(collision::cell_key((ix + 0.5f) * collision::cell_size, (iy + 0.5f) * collision::cell_size));
                if (cell != s.cells.end()) candidates.insert(candidates.end(), cell->second.begin(), cell->second.end());
            }
        std::sort(candidates.begin(), candidates.end());
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
        // Keep a face if any part of its bounds is in the range box. Rank by the distance to
        // its nearest point, so tall or wide faces are not dropped for a far-away centre.
        // Moving faces (entity cmodels posed this frame) go through the same tests.
        std::vector<std::pair<float, const collision::Face*>> chosen;
        auto consider = [&](const collision::Face& f) {
            if (f.hi.x < c.x - h || f.lo.x > c.x + h || f.hi.y < c.y - h || f.lo.y > c.y + h || f.hi.z < c.z - v || f.lo.z > c.z + v)
                return;
            if (!f.triangle && nav::dot(f.normal, f.centre - c) >= 0) return;
            const float dx = std::max({f.lo.x - c.x, 0.0f, c.x - f.hi.x}), dy = std::max({f.lo.y - c.y, 0.0f, c.y - f.hi.y}),
                        dz = std::max({f.lo.z - c.z, 0.0f, c.z - f.hi.z});
            chosen.push_back({dx * dx + dy * dy + dz * dz, &f});
        };
        for (const auto index : candidates)
            consider(s.faces[index]);
        for (const auto& f : moving)
            consider(f);
        if (chosen.size() > budget)
        {
            std::nth_element(chosen.begin(), chosen.begin() + budget, chosen.end(),
                             [](const auto& a, const auto& b) { return a.first < b.first; });
            chosen.resize(budget);
        }
        const float strength =
            std::clamp(settings.collision_opacity, 0.0f, 1.0f) * (frame_paused ? std::clamp(paused_strength, 0.0f, 1.0f) : 1.0f);
        const auto alpha = static_cast<unsigned>(strength * 255.0f + 0.5f);
        constexpr float depth_scale = collision_gpu::far_plane / (collision_gpu::far_plane - collision_gpu::near_plane);
        // offset moves the point along its eye ray by that many world units (positive is
        // away from the eye) without changing its screen position.
        auto vertex = [&](nav::Vec3 world, float offset, ImU32 color) {
            auto p = nav::camera_space(cam, world);
            if (const float length = std::sqrt(nav::dot(p, p)); length > 1) p = p * ((length + offset) / length);
            return collision_gpu::Vertex{-p.x / cam.tan_x, p.y / cam.tan_y, depth_scale * (p.z - collision_gpu::near_plane), p.z, color};
        };
        for (const auto& item : chosen)
        {
            const auto& f = *item.second;
            if (!f.blocking) continue; // non-blocking faces are not collision
            auto n = f.normal;
            const auto kind = f.kind;
            const auto to = f.centre - c;
            // Triangles are drawn two-sided and shaded by the side facing the eye. Their colour
            // follows the stored winding (floor stays floor when you are below it, e.g. prone:
            // 453 of 508 near-horizontal triangles near one test spot agree). Both sides draw
            // solid: the one pass-through observation (triangles 66884/66885) turned out to be
            // entity-owned, model-local data, so nothing shows world triangles are one-sided.
            // The back side only loses ties.
            bool back = false;
            if (f.triangle)
            {
                back = nav::dot(n, to) > 0;
                n = back ? n * -1.0f : n;
            }
            float r = 80, g = 200, b = 255;
            switch (kind)
            {
            case collision::Kind::floor:
                r = 80;
                g = 230;
                b = 120;
                break;
            case collision::Kind::roof:
                r = 255;
                g = 160;
                b = 60;
                break;
            case collision::Kind::clip:
                r = 255;
                g = 80;
                b = 255;
                break;
            default: break;
            }
            // Moving collision (doors, elevators, bus clips) in yellow, apart from the static
            // surfaces (user request 2026-10-30).
            if (f.moving)
            {
                r = 215;
                g = 215;
                b = 225;
            } // light grey (user request 2026-10-30, yellow before)
            if (!f.blocking)
            {
                r = 150;
                g = 150;
                b = 150;
            }
            const float distance = std::sqrt(nav::dot(to, to));
            // Shade from the normal against the view direction, so coplanar faces (several
            // brushes often share one wall plane) get the same colour and their depth ties
            // cannot show as shimmering stripes. It used the direction to each face's centre,
            // which differs per face (user report 2026-10-13, blue wall).
            const float shade = 0.6f + 0.4f * std::clamp(-nav::dot(n, cam.forward), 0.0f, 1.0f);
            const auto fill = IM_COL32(static_cast<int>(r * shade), static_cast<int>(g * shade), static_cast<int>(b * shade), alpha);
            const auto edge = IM_COL32(static_cast<int>(r * shade * 0.45f), static_cast<int>(g * shade * 0.45f),
                                       static_cast<int>(b * shade * 0.45f), std::min(255u, alpha + 40u));
            // Fixed draw priority for coincident faces: solid faces win over clip-only, which
            // win over back sides and non-blocking faces. The step is in world units, not a
            // share of the distance: a share let a floor 0.2 units under a clip brush win far
            // away and lose nearby, so the colours shifted as the player moved (user report
            // 2026-10-13). Faces more than a step apart always show the truly nearer one.
            const float push = !f.blocking ? 3.0f : back ? 2.0f : kind == collision::Kind::clip ? 1.0f : 0.0f;
            // Edges sit just in front of their own face (D24 depth resolves about 0.06 units at
            // 2000) and never reach the next priority step.
            const float edge_pull = push - std::min(0.3f + 0.0002f * distance, 0.8f);
            // The priority step moves the face away from the eye along its own normal, not along
            // the eye ray: along the ray, a clip brush 0.5 above terrain won at grazing angles and
            // lost up close, so the ground changed colour as you walked up (user report
            // 2026-10-30). Edges keep their small pull toward the eye along the ray.
            std::vector<nav::Vec3> p(f.points);
            for (auto& point : p)
                point = point - n * push;
            for (std::size_t i = 1; i + 1 < p.size(); ++i)
            {
                batch.fills.push_back(vertex(p[0], 0, fill));
                batch.fills.push_back(vertex(p[i], 0, fill));
                batch.fills.push_back(vertex(p[i + 1], 0, fill));
            }
            for (std::size_t i = 0; i < p.size(); ++i)
            {
                batch.lines.push_back(vertex(p[i], edge_pull - push, edge));
                batch.lines.push_back(vertex(p[(i + 1) % p.size()], edge_pull - push, edge));
            }
        }
    }

    // Living zombies' entity bounds as red boxes for the GPU pass (depth-tested with the
    // world collision when that is drawn too). Independent of the world collision view
    // (user request 2026-10-20). Drawn within 3000 units, at the collision opacity.
    void build_zombie_boxes(const nav::Camera& cam, collision_gpu::Batch& batch)
    {
        constexpr float depth_scale = collision_gpu::far_plane / (collision_gpu::far_plane - collision_gpu::near_plane);
        auto vertex = [&](nav::Vec3 world, float offset, ImU32 color) {
            auto p = nav::camera_space(cam, world);
            if (const float length = std::sqrt(nav::dot(p, p)); length > 1) p = p * ((length + offset) / length);
            return collision_gpu::Vertex{-p.x / cam.tan_x, p.y / cam.tan_y, depth_scale * (p.z - collision_gpu::near_plane), p.z, color};
        };
        const auto c = cam.origin;
        constexpr float h = 3000, v = 3000;
        const float strength = std::clamp(settings.collision_opacity, 0.0f, 1.0f) * (frame_paused ? paused_strength : 1.0f);
        const auto alpha = static_cast<unsigned>(strength * 255.0f + 0.5f);
        const auto z = zombies::snapshot();
        if (z && GetTickCount64() - z->updated <= 200)
        {
            const unsigned quads[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}};
            const nav::Vec3 normals[6] = {{0, 0, -1}, {0, 0, 1}, {0, -1, 0}, {1, 0, 0}, {0, 1, 0}, {-1, 0, 0}};
            for (const auto& marker : z->markers)
            {
                nav::Vec3 origin{}, mins{}, maxs{};
                if (!zombies::living_bounds(marker, origin, mins, maxs)) continue;
                if (std::abs(origin.x - c.x) > h || std::abs(origin.y - c.y) > h || std::abs(origin.z - c.z) > v) continue;
                const auto lo = origin + mins, hi = origin + maxs;
                const nav::Vec3 corners[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
                                              {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
                const auto centre = (lo + hi) * 0.5f, to = centre - c;
                const float distance = std::sqrt(nav::dot(to, to));
                for (unsigned q = 0; q < 6; ++q)
                {
                    const float shade = 0.45f + 0.55f * std::clamp(distance > 1 ? -nav::dot(normals[q], to) / distance : 1.0f, 0.0f, 1.0f);
                    // Faint fill: the zombie stands inside its own box, and a solid front face hid it
                    // (user report 2026-10-13). The edges carry the shape.
                    const auto fill =
                        IM_COL32(static_cast<int>(255 * shade), static_cast<int>(60 * shade), static_cast<int>(60 * shade), alpha / 5);
                    const auto edge = IM_COL32(static_cast<int>(255 * shade), static_cast<int>(70 * shade), static_cast<int>(70 * shade),
                                               std::min(255u, alpha + 40u));
                    const auto& f = quads[q];
                    for (unsigned i = 1; i < 3; ++i)
                    {
                        batch.fills.push_back(vertex(corners[f[0]], 0, fill));
                        batch.fills.push_back(vertex(corners[f[i]], 0, fill));
                        batch.fills.push_back(vertex(corners[f[i + 1]], 0, fill));
                    }
                    for (unsigned i = 0; i < 4; ++i)
                    {
                        batch.lines.push_back(vertex(corners[f[i]], -0.3f, edge));
                        batch.lines.push_back(vertex(corners[f[(i + 1) % 4]], -0.3f, edge));
                    }
                }
            }
        }
    }

    // NAV graph for the GPU pass (user request 2026-10-13: draw it under zombies). Same
    // selection and look as the ImGui drawing in draw(): links (reciprocal pairs once,
    // traversal arrows), then node symbols sized in pixels.
    void build_nav(const nav::Camera& cam, const nav::Graph& graph, float sx, float sy, collision_gpu::Batch& batch)
    {
        constexpr float depth_scale = collision_gpu::far_plane / (collision_gpu::far_plane - collision_gpu::near_plane);
        auto vertex = [&](nav::Vec3 p, ImU32 color) { // p in camera space
            return collision_gpu::Vertex{-p.x / cam.tan_x, p.y / cam.tan_y, depth_scale * (p.z - collision_gpu::near_plane), p.z, color};
        };
        // Clip-space point moved by a pixel offset (screen y down).
        auto offset = [&](collision_gpu::Vertex v, float dx, float dy) {
            v.x += dx * 2 / batch.viewport_width * v.w;
            v.y -= dy * 2 / batch.viewport_height * v.w;
            return v;
        };
        const auto& nodes_list = graph.nodes;
        std::vector<nav::Vec3> projected(nodes_list.size());
        std::vector<unsigned char> keep(nodes_list.size());
        for (std::size_t i = 0; i < nodes_list.size(); ++i)
        {
            keep[i] = static_cast<unsigned char>(settings.nav_nodes &&
                                                 nav::within_draw_range(nodes_list[i].origin, cam.origin, settings.nav_range,
                                                                        vertical(settings.nav_range))); // links need nodes on, as in draw()
            if (keep[i]) projected[i] = nav::camera_space(cam, nodes_list[i].origin);
        }
        // Behind the pause menu the GPU copy dims like the ImGui drawing (fade()).
        const float strength = frame_paused ? paused_strength : 1.0f;
        const ImU32 link_color = IM_COL32(90, 205, 229, static_cast<int>(150 * strength));
        if (settings.nav_links)
            for (std::size_t i = 0; i < nodes_list.size(); ++i)
            {
                if (!keep[i]) continue;
                for (const auto dest : nodes_list[i].links)
                {
                    if (!keep[dest]) continue;
                    const bool crossing = nav::traversal_crossing(nodes_list[i].type, nodes_list[dest].type);
                    const bool opposite_crossing = nav::traversal_crossing(nodes_list[dest].type, nodes_list[i].type);
                    if (dest < i && !(settings.nav_directions && (crossing || opposite_crossing)) &&
                        std::find(nodes_list[dest].links.begin(), nodes_list[dest].links.end(), static_cast<std::uint16_t>(i)) !=
                            nodes_list[dest].links.end())
                        continue;
                    auto a = projected[i], b = projected[dest];
                    if (!nav::clip_segment(cam, a, b)) continue;
                    batch.overlay_lines.push_back(vertex(a, link_color));
                    batch.overlay_lines.push_back(vertex(b, link_color));
                    if (settings.nav_directions && crossing)
                    {
                        const auto p = screen(nav::project(cam, a), sx, sy), q = screen(nav::project(cam, b), sx, sy);
                        const float dx = q.x - p.x, dy = q.y - p.y, len = std::sqrt(dx * dx + dy * dy);
                        if (len > 0.001f)
                        {
                            const float ux = dx / len, uy = dy / len;
                            const auto from = nodes_list[i].origin, to = nodes_list[dest].origin;
                            for (const float fraction : {0.25f, 0.50f, 0.75f})
                            {
                                const auto anchor = nav::camera_space(cam, from + (to - from) * fraction);
                                if (!nav::point_visible(cam, anchor)) continue;
                                const auto tip = vertex(anchor, link_color);
                                batch.overlay_lines.push_back(tip);
                                batch.overlay_lines.push_back(offset(tip, -ux * 6 - uy * 3, -uy * 6 + ux * 3));
                                batch.overlay_lines.push_back(tip);
                                batch.overlay_lines.push_back(offset(tip, -ux * 6 + uy * 3, -uy * 6 - ux * 3));
                            }
                        }
                    }
                }
            }
        if (settings.nav_nodes)
            for (std::size_t i = 0; i < nodes_list.size(); ++i)
            {
                if (!keep[i] || !nav::point_visible(cam, projected[i])) continue;
                const auto type = nodes_list[i].type;
                const ImU32 color = IM_COL32(174, 150, 255, static_cast<int>(235 * strength));
                const auto c = vertex(projected[i], color);
                auto& f = batch.overlay_fills;
                if (type == 17)
                { // filled diamond
                    const auto top = offset(c, 0, -6.75f), right = offset(c, 6.75f, 0), bottom = offset(c, 0, 6.75f),
                               left = offset(c, -6.75f, 0);
                    f.insert(f.end(), {top, right, bottom, top, bottom, left});
                }
                else if (type == 18)
                { // square outline, 2.25 px thick
                    constexpr float o = 6 + 1.125f, in = 6 - 1.125f;
                    const float outer[4][2] = {{-o, -o}, {o, -o}, {o, o}, {-o, o}},
                                inner[4][2] = {{-in, -in}, {in, -in}, {in, in}, {-in, in}};
                    for (int k = 0; k < 4; ++k)
                    {
                        const int n = (k + 1) % 4;
                        const auto a = offset(c, outer[k][0], outer[k][1]), b = offset(c, outer[n][0], outer[n][1]),
                                   d = offset(c, inner[k][0], inner[k][1]), e = offset(c, inner[n][0], inner[n][1]);
                        f.insert(f.end(), {a, b, e, a, e, d});
                    }
                }
                else
                { // dot, radius 2.5
                    for (int k = 0; k < 8; ++k)
                    {
                        const float a0 = 6.2831853f * k / 8, a1 = 6.2831853f * (k + 1) / 8;
                        f.insert(f.end(), {c, offset(c, 2.5f * std::cos(a0), 2.5f * std::sin(a0)),
                                           offset(c, 2.5f * std::cos(a1), 2.5f * std::sin(a1))});
                    }
                }
            }
    }

    namespace
    {
        // In-world label: TTF text with a 1-pixel shadow.
        void label(ImDrawList* list, float size, ImVec2 at, ImU32 color, const char* text)
        {
            list->AddText(fonts::get(size), size, ImVec2(at.x + 1, at.y + 1), IM_COL32(0, 0, 0, 200), text);
            list->AddText(fonts::get(size), size, at, color, text);
        }

        // Fills a projected convex polygon whatever its winding on screen.
        void fill_convex(ImDrawList* list, std::vector<ImVec2>& pixels, ImU32 color)
        {
            float area = 0;
            for (std::size_t i = 0; i < pixels.size(); ++i)
            {
                const auto& a = pixels[i];
                const auto& b = pixels[(i + 1) % pixels.size()];
                area += a.x * b.y - b.x * a.y;
            }
            if (area < 0) std::reverse(pixels.begin(), pixels.end());
            list->AddConvexPolyFilled(pixels.data(), static_cast<int>(pixels.size()), color);
        }

        // NAV graph: links (a reciprocal pair once unless direction arrows are on), node symbols
        // and ID labels. When the injection already drew links and symbols on the GPU (under
        // zombies), only the ID labels are drawn here.
        void draw_nav(ImDrawList* list, const nav::Camera& cam, const nav::Graph& g, float sx, float sy, float label_size)
        {
            std::vector<nav::Vec3> projected(g.nodes.size());
            std::vector<unsigned char> keep(g.nodes.size());
            for (std::size_t i = 0; i < g.nodes.size(); ++i)
            {
                const auto& n = g.nodes[i];
                keep[i] = static_cast<unsigned char>(
                    settings.nav_nodes && nav::within_draw_range(n.origin, cam.origin, settings.nav_range, vertical(settings.nav_range)));
                if (keep[i]) projected[i] = nav::camera_space(cam, n.origin);
            }
            const bool gpu_nav = nav_injected && (in_injection || frame_trace::injected_last_frame());
            if (settings.nav_links && !gpu_nav)
                for (std::size_t i = 0; i < g.nodes.size(); ++i)
                {
                    if (!keep[i]) continue;
                    for (const auto dest : g.nodes[i].links)
                    {
                        if (!keep[dest]) continue;
                        // One-way edges are preserved regardless of index ordering.
                        const bool crossing = nav::traversal_crossing(g.nodes[i].type, g.nodes[dest].type);
                        const bool opposite_crossing = nav::traversal_crossing(g.nodes[dest].type, g.nodes[i].type);
                        if (dest < i && !(settings.nav_directions && (crossing || opposite_crossing)) &&
                            std::find(g.nodes[dest].links.begin(), g.nodes[dest].links.end(), static_cast<std::uint16_t>(i)) !=
                                g.nodes[dest].links.end())
                            continue;
                        auto a = projected[i], b = projected[dest];
                        if (!nav::clip_segment(cam, a, b)) continue;
                        const auto p = screen(nav::project(cam, a), sx, sy), q = screen(nav::project(cam, b), sx, sy);
                        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(q.x) || !std::isfinite(q.y)) continue;
                        const ImU32 link_color = IM_COL32(90, 205, 229, 150);
                        list->AddLine(p, q, link_color, crossing ? 1.5f : 1);
                        if (settings.nav_directions && crossing)
                        {
                            const float dx = q.x - p.x, dy = q.y - p.y, len = std::sqrt(dx * dx + dy * dy);
                            // Anchor in world space before clipping or projection.
                            // A screen fraction slides as perspective/clipped ends vary.
                            const auto from = g.nodes[i].origin, to = g.nodes[dest].origin;
                            if (len > 0.001f)
                            {
                                const float ux = dx / len, uy = dy / len;
                                for (const float fraction : {0.25f, 0.50f, 0.75f})
                                {
                                    const auto anchor = nav::camera_space(cam, from + (to - from) * fraction);
                                    if (!nav::point_visible(cam, anchor)) continue;
                                    const auto tip = screen(nav::project(cam, anchor), sx, sy);
                                    list->AddLine(tip, ImVec2(tip.x - ux * 6 - uy * 3, tip.y - uy * 6 + ux * 3), link_color, 2);
                                    list->AddLine(tip, ImVec2(tip.x - ux * 6 + uy * 3, tip.y - uy * 6 - ux * 3), link_color, 2);
                                }
                            }
                        }
                    }
                }
            unsigned label_count = 0;
            for (std::size_t i = 0; i < g.nodes.size(); ++i)
            {
                if (!keep[i] || !nav::point_visible(cam, projected[i])) continue;
                const auto p = screen(nav::project(cam, projected[i]), sx, sy);
                const auto type = g.nodes[i].type;
                const ImU32 color = IM_COL32(174, 150, 255, 235);
                if (!gpu_nav)
                {
                    if (type == 17)
                        list->AddQuadFilled(ImVec2(p.x, p.y - 6.75f), ImVec2(p.x + 6.75f, p.y), ImVec2(p.x, p.y + 6.75f),
                                            ImVec2(p.x - 6.75f, p.y), color);
                    else if (type == 18)
                        list->AddRect(ImVec2(p.x - 6, p.y - 6), ImVec2(p.x + 6, p.y + 6), color, 0, 0, 2.25f);
                    else
                        list->AddCircleFilled(p, 2.5f, color, 8);
                }
                if (settings.nav_ids && label_count < 120 && projected[i].z < 900)
                {
                    char id[48]{};
                    if (type == 1)
                        std::snprintf(id, sizeof(id), "%u", static_cast<unsigned>(i));
                    else if (type == 17 || type == 18)
                        std::snprintf(id, sizeof(id), "%u %s", static_cast<unsigned>(i), type == 17 ? "B" : "E");
                    else
                        std::snprintf(id, sizeof(id), "%u t%u", static_cast<unsigned>(i), type);
                    label(list, label_size, ImVec2(p.x + ((type == 17 || type == 18) ? 9 : 5), p.y - label_size * 0.5f), color, id);
                    ++label_count;
                }
            }
        }

        // Zombie and special spawn sites in range, counted into frame.spawn_count for the menu.
        void draw_spawn_sites(ImDrawList* list, const nav::Camera& cam, FrameInfo& frame, float sx, float sy, float label_size)
        {
            // Spawn-site label under its symbol, starting at the symbol's left edge.
            auto under_symbol = [&](ImVec2 p, float radius, ImU32 color, const char* text) {
                label(list, label_size, ImVec2(p.x - radius, p.y + radius + 2), color, text);
            };
            constexpr float symbol_scale = 0.67f;
            const auto drawing_sites = spawns::drawing_sites(frame.spawn_snapshot->sites);
            // Distinct configured objects can share an exact origin. Draw
            // eligible sites last so a red alternative cannot cover green.
            for (int active_pass = 0; active_pass < 2; ++active_pass)
            {
                for (auto site : drawing_sites)
                {
                    site.types = spawns::visible_types(site.types, settings.special_spawns);
                    if (!site.types) continue;
                    if (site.active != (active_pass == 1)) continue;
                    if (settings.active_spawns_only && !site.active) continue;
                    if (!nav::within_draw_range(site.origin, cam.origin, settings.spawn_range, vertical(settings.spawn_range))) continue;
                    const auto relative = nav::camera_space(cam, site.origin);
                    if (!nav::point_visible(cam, relative)) continue;
                    const auto p = screen(nav::project(cam, relative), sx, sy);
                    const ImU32 color = site.active ? IM_COL32(65, 235, 115, 255) : IM_COL32(150, 155, 165, 255);
                    if ((site.types & 5) == 5)
                    {
                        list->AddCircleFilled(p, 11 * symbol_scale, IM_COL32(0, 0, 0, 255), 24);
                        list->AddCircleFilled(p, 8.5f * symbol_scale, color, 24);
                        const ImVec2 top(p.x, p.y - 5 * symbol_scale), east(p.x + 5 * symbol_scale, p.y),
                            bottom(p.x, p.y + 5 * symbol_scale), west(p.x - 5 * symbol_scale, p.y);
                        list->AddQuad(top, east, bottom, west, IM_COL32(0, 0, 0, 255), 2 * symbol_scale);
                        list->AddQuadFilled(top, east, bottom, west, IM_COL32(195, 145, 255, 255));
                        if (settings.spawn_labels)
                        {
                            const auto text = spawns::site_label(site.types);
                            under_symbol(p, 11 * symbol_scale, IM_COL32(230, 205, 255, 255), text.c_str());
                        }
                    }
                    else if ((site.types & 32) && !(site.types & 16))
                    {
                        const float radius = 7 * symbol_scale;
                        const ImVec2 upper(p.x - radius, p.y - radius), lower(p.x + radius, p.y + radius);
                        list->AddRectFilled(upper, lower, IM_COL32(195, 145, 255, 255));
                        list->AddRect(upper, lower, IM_COL32(0, 0, 0, 255), 0, 0, 3 * symbol_scale);
                        const auto site_label = spawns::site_label(site.types);
                        if (settings.spawn_labels) under_symbol(p, radius, IM_COL32(230, 205, 255, 255), site_label.c_str());
                        if (site.types & 1) list->AddCircleFilled(p, 3 * symbol_scale, color, 16);
                    }
                    else if (site.types & 254)
                    {
                        const ImVec2 top(p.x, p.y - 9 * symbol_scale), east(p.x + 9 * symbol_scale, p.y),
                            bottom(p.x, p.y + 9 * symbol_scale), west(p.x - 9 * symbol_scale, p.y);
                        list->AddQuad(top, east, bottom, west, IM_COL32(0, 0, 0, 255), 3 * symbol_scale);
                        // Lavender identifies a registered special site, not active chooser membership.
                        // Avogadro sites are electric blue, apart from TranZit's Denizen sites (2026-10-30).
                        list->AddQuadFilled(top, east, bottom, west,
                                            site.types & 128 ? IM_COL32(90, 200, 255, 255) : IM_COL32(195, 145, 255, 255));
                        const auto site_label = spawns::site_label(site.types);
                        if (settings.spawn_labels) under_symbol(p, 9 * symbol_scale, IM_COL32(230, 205, 255, 255), site_label.c_str());
                        if (site.types & 1) list->AddCircleFilled(p, 3 * symbol_scale, color, 16);
                    }
                    else
                    {
                        list->AddCircleFilled(p, 9 * symbol_scale, IM_COL32(0, 0, 0, 255), 24);
                        list->AddCircleFilled(p, 6.5f * symbol_scale, color, 24);
                        if (settings.spawn_labels) under_symbol(p, 9 * symbol_scale, IM_COL32(240, 240, 245, 255), "Zombie");
                    }
                    ++frame.spawn_count;
                }
            }
        }

        // Trigger volumes and the playable area. A shape is drawn whole when any part overlaps
        // the radius.
        void draw_triggers(ImDrawList* list, const nav::Camera& cam, const FrameInfo& frame, float sx, float sy, float label_size)
        {
            auto in_range = [&](const triggers::Marker& marker, nav::Vec3 origin, nav::Vec3 angles) {
                if (marker.shape == "Cylinder")
                    return triggers::cylinder_range_overlap(origin, marker.maxs.x, marker.maxs.z, cam.origin, settings.trigger_range,
                                                            vertical(settings.trigger_range));
                for (const auto& mesh : marker.meshes)
                    if (triggers::range_overlap(mesh, cam.origin, settings.trigger_range, vertical(settings.trigger_range),
                                                [&](nav::Vec3 p) { return origin + zombies::rotate(p, angles); }))
                        return true;
                return false;
            };
            auto safe_role = [](triggers::Region role) {
                return role == triggers::Region::enabled_player || role == triggers::Region::life;
            };
            // Playable area: the merged surface of every enabled and life volume
            // (playable_surface), cut to the draw radius. Nearby surface reads strongest,
            // far surface fades to a quarter.
            if (settings.playable_area)
            {
                const auto& surface = playable_surface(frame.trigger_snapshot);
                const auto c = cam.origin;
                const float hr = settings.trigger_range, vr = vertical(settings.trigger_range);
                auto close_by = [&](nav::Vec3 lo, nav::Vec3 hi) {
                    return hi.x >= c.x - hr && lo.x <= c.x + hr && hi.y >= c.y - hr && lo.y <= c.y + hr && hi.z >= c.z - vr &&
                           lo.z <= c.z + vr;
                };
                auto faded = [&](ImU32 color, nav::Vec3 at) {
                    const auto to = at - c;
                    const float fade = std::clamp(1.0f - std::sqrt(nav::dot(to, to)) / settings.trigger_range, 0.25f, 1.0f);
                    return (color & ~IM_COL32_A_MASK) |
                           (static_cast<ImU32>(static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFF) * fade) << IM_COL32_A_SHIFT);
                };
                for (const auto& piece : surface.pieces)
                {
                    if (!close_by(piece.lo, piece.hi)) continue;
                    std::vector<nav::Vec3> corners;
                    for (const auto p : piece.points)
                        corners.push_back(nav::camera_space(cam, p));
                    std::vector<ImVec2> pixels;
                    for (const auto p : nav::clip_polygon(cam, corners))
                        pixels.push_back(screen(nav::project(cam, p), sx, sy));
                    if (pixels.size() < 3) continue;
                    fill_convex(list, pixels, faded(IM_COL32(45, 245, 95, 16), (piece.lo + piece.hi) * 0.5f));
                }
                for (const auto& edge : surface.edges)
                {
                    const nav::Vec3 lo{std::min(edge.a.x, edge.b.x), std::min(edge.a.y, edge.b.y), std::min(edge.a.z, edge.b.z)};
                    const nav::Vec3 hi{std::max(edge.a.x, edge.b.x), std::max(edge.a.y, edge.b.y), std::max(edge.a.z, edge.b.z)};
                    if (!close_by(lo, hi)) continue;
                    auto a = nav::camera_space(cam, edge.a), b = nav::camera_space(cam, edge.b);
                    if (!nav::clip_segment(cam, a, b)) continue;
                    const auto color = edge.life ? IM_COL32(135, 255, 165, 170) : IM_COL32(45, 235, 95, 120);
                    list->AddLine(screen(nav::project(cam, a), sx, sy), screen(nav::project(cam, b), sx, sy),
                                  faded(color, (edge.a + edge.b) * 0.5f), 1.0f);
                }
            }
            for (const auto& marker : frame.trigger_snapshot->markers)
            {
                const auto role = triggers::region(marker.fields);
                const bool area_region = settings.playable_area && triggers::playable_region(role);
                const bool general = marker.name != "info_volume" && settings.trigger_geometry;
                if (!area_region && !general) continue;
                if (area_region && safe_role(role) && !general) continue; // part of the merged surface
                const bool other = settings.playable_area && !area_region;
                const auto fill = other ? IM_COL32(170, 175, 185, 12) : IM_COL32(255, 40, 40, 24);
                const auto outline = other ? IM_COL32(170, 175, 185, 120) : IM_COL32(255, 65, 65, 185);
                const auto label_color = other ? IM_COL32(195, 200, 210, 200) : IM_COL32(255, 150, 150, 230);
                nav::Vec3 origin{}, angles{};
                if (!triggers::current_pose(marker, origin, angles)) continue;
                if (!in_range(marker, origin, angles)) continue;
                bool drawn = false;
                for (const auto& mesh : marker.meshes)
                    for (const auto& face : mesh.faces)
                    {
                        std::vector<nav::Vec3> corners;
                        std::vector<ImVec2> pixels;
                        for (const auto vertex : face)
                            corners.push_back(
                                nav::camera_space(cam, origin + (marker.shape == "Cylinder" ? vertex : zombies::rotate(vertex, angles))));
                        for (const auto point : nav::clip_polygon(cam, corners))
                            pixels.push_back(screen(nav::project(cam, point), sx, sy));
                        if (pixels.size() >= 3) fill_convex(list, pixels, fill);
                        for (std::size_t i = 0; i < corners.size(); ++i)
                        {
                            auto a = corners[i], b = corners[(i + 1) % corners.size()];
                            if (!nav::clip_segment(cam, a, b)) continue;
                            list->AddLine(screen(nav::project(cam, a), sx, sy), screen(nav::project(cam, b), sx, sy), outline, 1.0f);
                            drawn = true;
                        }
                    }
                const auto center = nav::camera_space(cam, origin);
                if (settings.trigger_labels && drawn && nav::point_visible(cam, center))
                {
                    const auto p = screen(nav::project(cam, center), sx, sy);
                    const auto text = triggers::label(marker.name, marker.fields);
                    label(list, label_size, ImVec2(p.x + 6, p.y + 6), label_color, text.c_str());
                }
            }
        }

        // Collision at Present, for frames the injection did not draw: built into
        // collision_batch for the GPU pass (render() submits it), or drawn as ImGui polygons
        // when the GPU pass is unavailable. Zombie hitboxes join the batch too.
        void build_present_collision(ImDrawList* list, const nav::Camera& cam, float sx, float sy)
        {
            collision::enable(settings.world_collision);
            collision_batch.clear();
            collision_gpu_pass = false;
            if (settings.world_collision)
                if (const auto walls = collision::snapshot())
                {
                    collision_gpu_failed = !collision_gpu::ready(device);
                    if (collision_gpu_failed)
                        draw_collision(list, cam, *walls, sx, sy);
                    else if (!frame_trace::injected_last_frame()) // else the injection already drew it
                    {
                        std::vector<collision::Face> moving;
                        if (collision_moving)
                            collision::movers(*walls, cam.origin, settings.collision_range, moving, collision_model_bounds);
                        build_collision(cam, *walls, moving, sx, sy, collision_batch);
                        collision_gpu_pass = !collision_batch.fills.empty();
                    }
                }
            // Zombie hitboxes without world collision, when the injection did not run.
            if (settings.zombie_boxes && !frame_trace::injected_last_frame() && collision_gpu::ready(device))
            {
                collision_batch.viewport_x = cam.x * sx;
                collision_batch.viewport_y = cam.y * sy;
                collision_batch.viewport_width = cam.width * sx;
                collision_batch.viewport_height = cam.height * sy;
                build_zombie_boxes(cam, collision_batch);
                collision_gpu_pass = !collision_batch.fills.empty();
            }
        }

        // Wallhack: an arrow above each living zombie (a diamond for special enemies), coloured
        // by the despawn tracker when it is on, with an eye when only the view cone protects it.
        void draw_zombie_markers(ImDrawList* list, const nav::Camera& cam, const FrameInfo& frame, const nav::Graph& g, float sx, float sy)
        {
            const auto z = zombies::snapshot();
            if (!z || GetTickCount64() - z->updated > 200 || z->map->map != g.map || z->map->base != g.base || z->map->data != g.data)
                return;
            for (const auto& marker : z->markers)
            {
                if (!despawn::ui::arrow_visible(frame.despawn_view, marker.number)) continue;
                nav::Vec3 origin{};
                if (!zombies::still_living(marker, origin)) continue;
                const auto relative = nav::camera_space(cam, zombies::marker_anchor(origin));
                if (!nav::point_visible(cam, relative)) continue;
                const auto p = screen(nav::project(cam, relative), sx, sy);
                // Fixed 12-pixel arrow with one arrow-height clear gap above
                // the projected standing head. No skeleton/fallback switching.
                const ImVec2 left(p.x - 6, p.y - 24), right(p.x + 6, p.y - 24), tip(p.x, p.y - 12);
                // Draw the black border first so the red interior stays intact.
                const auto marker_color = despawn::ui::arrow_color(frame.despawn_view, marker.number);
                if (marker.special)
                {
                    const ImVec2 top(p.x, p.y - 28), east(p.x + 8, p.y - 20), bottom(p.x, p.y - 12), west(p.x - 8, p.y - 20);
                    list->AddQuad(top, east, bottom, west, IM_COL32(0, 0, 0, 255), 3.0f);
                    list->AddQuadFilled(top, east, bottom, west, marker_color);
                }
                else
                {
                    list->AddTriangle(left, right, tip, IM_COL32(0, 0, 0, 255), 3.0f);
                    list->AddTriangleFilled(left, right, tip, marker_color);
                }
                if (despawn::ui::arrow_eye(frame.despawn_view, marker.number))
                {
                    // Center above either marker, including its black outline,
                    // with clear space above the taller special-enemy diamond.
                    const ImVec2 eye(p.x, p.y - 40);
                    list->AddEllipse(eye, ImVec2(8, 5), IM_COL32(0, 0, 0, 255), 0, 24, 4);
                    list->AddEllipse(eye, ImVec2(8, 5), IM_COL32(255, 245, 215, 255), 0, 24, 2);
                    list->AddCircleFilled(eye, 2.5f, IM_COL32(255, 245, 215, 255));
                }
            }
        }
    } // namespace

    void draw(ImDrawList* list, float width, float height, FrameInfo& frame, bool world, bool present_part)
    {
        if (!frame.camera_valid) return;
        const auto& g = *frame.graph;
        const auto& cam = frame.camera;
        // In-world labels, a little larger at high resolution (25% smaller since 2026-10-13, user).
        const float label_size = std::max(11.0f, 11.25f * height / 1080.0f);
        // Refdef viewport is in render pixels. Scale the complete viewport to
        // the swap-chain backbuffer, including its offset.
        const float sx = width / static_cast<float>(cam.x + cam.width);
        const float sy = height / static_cast<float>(cam.y + cam.height);
        list->PushClipRect(ImVec2(cam.x * sx, cam.y * sy), ImVec2(width, height), true);
        if (world) draw_nav(list, cam, g, sx, sy, label_size);
        if (world && settings.spawn_points && frame.spawn_valid) draw_spawn_sites(list, cam, frame, sx, sy, label_size);
        if (world && (settings.trigger_geometry || settings.playable_area) && frame.trigger_snapshot &&
            triggers::fresh(*frame.trigger_snapshot, g, GetTickCount64()))
            draw_triggers(list, cam, frame, sx, sy, label_size);
        if (present_part) build_present_collision(list, cam, sx, sy);
        if (world && frame.player_valid) draw_player_box(list, cam, frame.player, sx, sy);
        // Origins-only aids: never drawn on another map, even if left switched on.
        // Drawn at Present, over the weapon and HUD (user request 2026-10-20): the rings and
        // readouts must stay readable over the held staff.
        if (present_part && (settings.wind_aid || settings.fire_aid) && g.map == "maps/mp/zm_tomb.d3dbsp")
        {
            const auto staffs = staff::snapshot();
            if (settings.wind_aid) draw_wind(list, cam, staffs, width, height, sx, sy, settings.wind_details);
            if (settings.fire_aid) draw_fire(list, cam, staffs, width, height, sx, sy, settings.fire_details);
        }
        // Robot footstep rings in the world pass (under the weapon), their readout at Present.
        if (settings.robot_steps && (world || present_part) && g.map == "maps/mp/zm_tomb.d3dbsp")
        {
            const auto robots = robot::snapshot();
            if (world) draw_robot_world(list, cam, robots, sx, sy);
            if (present_part && settings.robot_details) draw_robot_text(list, robots, width, height);
        }
        if (world && (settings.mob_skulls || settings.mob_shock_boxes || settings.mob_afterlife_spawns) &&
            g.map == "maps/mp/zm_prison.d3dbsp")
            draw_mob(list, cam, mob::snapshot(), height, sx, sy);
        if (world && settings.grenade_arcs) draw_grenades(list, cam, grenade::snapshot(), sx, sy);
        // TranZit Avogadro aid: route, dashes and markers in the world pass, its text at Present.
        if ((settings.avogadro_aid || settings.avogadro_spawns) && (world || present_part) && g.map == "maps/mp/zm_transit.d3dbsp")
        {
            const auto avogadro_view = avogadro::snapshot();
            if (world) draw_avogadro_world(list, cam, g, avogadro_view, label_size, sx, sy);
            if (present_part && settings.avogadro_aid) draw_avogadro_text(list, cam, g, avogadro_view, width, height, sx, sy);
        }
        if (world && settings.wallhack) draw_zombie_markers(list, cam, frame, g, sx, sy);
        list->PopClipRect();
    }
} // namespace overlay
