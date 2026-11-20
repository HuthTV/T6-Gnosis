// Renderer side of the overlay: hooks IDXGISwapChain::Present and ResizeBuffers (found
// through a throwaway swap chain), owns the ImGui context and window input, and runs the
// frame: the world drawings go in under the HUD through the in-frame injection
// (frame_trace), the menu, HUD and Present-time parts are drawn at Present.
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <MinHook.h>
#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "overlay.hpp"
#include "overlay_shared.hpp"
#include "fonts.hpp"
#include "frame_trace.hpp"
#include "gsc_args.hpp"
#include "zombies.hpp"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dwmapi.lib")
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace overlay
{
    ID3D11Device* device = nullptr;
    bool menu_open = false, frame_paused = false, nav_injected = false, in_injection = false;
    collision_gpu::Batch collision_batch;
    bool collision_gpu_pass = false, collision_gpu_failed = false;

    namespace
    {
        using Present = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
        using Resize = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
        Present next_present = nullptr;
        Resize next_resize = nullptr;
        void* present_address = nullptr;
        void* resize_address = nullptr;
        IDXGISwapChain* chain = nullptr;
        ID3D11DeviceContext* context = nullptr;
        ID3D11RenderTargetView* target = nullptr;
        ImGuiContext* gui = nullptr;
        HWND window = nullptr;
        WNDPROC previous_proc = nullptr;
        std::recursive_mutex guard;
        std::atomic<bool> stopping{false};
        std::atomic<bool> input_active{false}, moving_window{false};
        // Present's despawn view, reused by the injection (capture() also advances the
        // despawn pass counter, so it runs once per frame).
        despawn::ui::View last_despawn_view;
        unsigned backbuffer_width = 0, backbuffer_height = 0;
        // Cursor clip saved while the menu is open, and mouse wheel input for ImGui.
        bool clip_saved = false;
        RECT previous_clip{};
        float wheel = 0;
        std::uint64_t settings_checked = 0;
        // Recent server boxes per zombie entity number. The scene shows client-interpolated
        // positions that trail the server entity, so a running zombie's occlusion box spans
        // its last trail_ms of positions instead of only the newest one.
        constexpr std::uint64_t trail_ms = 150;

        struct Trail
        {
            std::uint32_t actor = 0;
            unsigned next = 0;
            std::uint64_t time[32]{};
            nav::Vec3 lo[32]{}, hi[32]{};
        };

        std::unordered_map<std::uint16_t, Trail> trails;
        // Drawing under the HUD: injected after the scene composite and tested against the
        // scene depth (frame_trace). scene_near converts reversed depth to distance.
        constexpr float scene_near = 4;
        // The last injection drew the world overlays (draw()'s world part) under the weapon
        // and HUD. world_spawn_count carries that drawing's spawn count to the F8 menu, which
        // Present builds without the world part.
        bool world_injected = false;
        unsigned world_spawn_count = 0;

        ULONGLONG last_frame = 0;

        template<class T> void release(T*& p)
        {
            if (p)
            {
                p->Release();
                p = nullptr;
            }
        }

        void close_input()
        {
            cancel_key_capture();
            input_active = false;
            if (gui)
            {
                ImGui::GetIO().ClearInputMouse();
                ImGui::GetIO().ClearInputKeys();
                ImGui::GetIO().MouseDrawCursor = false;
            }
            if (clip_saved && GetForegroundWindow() == window) ClipCursor(&previous_clip);
            clip_saved = false;
            wheel = 0;
        }

        LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
        {
            if (message == WM_ENTERSIZEMOVE) moving_window = true;
            if (message == WM_EXITSIZEMOVE) moving_window = false;
            const bool client_input = message == WM_INPUT || message == WM_CHAR || (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) ||
                                      (message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
                                      (message == WM_SETCURSOR && LOWORD(lparam) == HTCLIENT);
            if (client_input && !stopping && !moving_window && input_active)
            {
                std::unique_lock lock(guard, std::try_to_lock);
                if (lock.owns_lock() && gui && menu_open)
                {
                    // Hand F9 to the existing timer/menu hook and release our capture.
                    if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && wparam == VK_F9)
                    {
                        auto saved = ImGui::GetCurrentContext();
                        ImGui::SetCurrentContext(gui);
                        menu_open = false;
                        close_input();
                        ImGui::SetCurrentContext(saved);
                    }
                    else
                    {
                        auto saved = ImGui::GetCurrentContext();
                        ImGui::SetCurrentContext(gui);
                        if (message == WM_INPUT)
                        {
                            RAWINPUT raw{};
                            UINT size = sizeof(raw);
                            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) !=
                                    static_cast<UINT>(-1) &&
                                raw.header.dwType == RIM_TYPEMOUSE && (raw.data.mouse.usButtonFlags & RI_MOUSE_WHEEL))
                                wheel += static_cast<SHORT>(raw.data.mouse.usButtonData) / static_cast<float>(WHEEL_DELTA);
                        }
                        else if (message == WM_MOUSEWHEEL)
                            wheel += static_cast<SHORT>(HIWORD(wparam)) / static_cast<float>(WHEEL_DELTA);
                        else if (!(message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) && message != WM_MOUSELEAVE)
                            ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam);
                        ImGui::SetCurrentContext(saved);
                        if (message == WM_INPUT) return DefWindowProcW(hwnd, message, wparam, lparam);
                        return message == WM_SETCURSOR ? TRUE : 0;
                    }
                }
            }
            return previous_proc ? CallWindowProcW(previous_proc, hwnd, message, wparam, lparam)
                                 : DefWindowProcW(hwnd, message, wparam, lparam);
        }

        void cleanup()
        {
            if (gui)
            {
                ImGui::SetCurrentContext(gui);
                close_input();
                ImGui_ImplDX11_Shutdown();
                ImGui_ImplWin32_Shutdown();
                ImGui::DestroyContext(gui);
                gui = nullptr;
                fonts::forget();
            }
            if (window && previous_proc && reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC)) == window_proc)
                SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previous_proc));
            frame_trace::uninstall();
            collision_gpu::release();
            release(target);
            release(context);
            release(device);
            release(chain);
            window = nullptr;
        }

        bool attach(IDXGISwapChain* candidate)
        {
            DXGI_SWAP_CHAIN_DESC desc{};
            DWORD owner = 0;
            if (FAILED(candidate->GetDesc(&desc)) || !IsWindowVisible(desc.OutputWindow)) return false;
            GetWindowThreadProcessId(desc.OutputWindow, &owner);
            if (owner != GetCurrentProcessId()) return false;
            if (FAILED(candidate->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device)))) return false;
            device->GetImmediateContext(&context);
            // No ImGui ini or log files: the menu's position and size are saved in T6Gnosis.cfg.
            gui = ImGui::CreateContext();
            ImGui::GetIO().IniFilename = nullptr;
            ImGui::GetIO().LogFilename = nullptr;
            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            ImGui::StyleColorsDark();
            auto& colors = ImGui::GetStyle().Colors;
            colors[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.045f, 0.115f, 0.98f);
            colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.065f, 0.15f, 0.98f);
            colors[ImGuiCol_Border] = ImVec4(0.40f, 0.25f, 0.57f, 0.65f);
            colors[ImGuiCol_Text] = ImVec4(0.95f, 0.92f, 0.99f, 1.0f);
            colors[ImGuiCol_TextDisabled] = ImVec4(0.62f, 0.55f, 0.72f, 1.0f);
            colors[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.065f, 0.19f, 1.0f);
            colors[ImGuiCol_TitleBgActive] = ImVec4(0.25f, 0.12f, 0.38f, 1.0f);
            colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.12f, 0.29f, 1.0f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.34f, 0.20f, 0.47f, 1.0f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.43f, 0.25f, 0.60f, 1.0f);
            colors[ImGuiCol_Button] = ImVec4(0.29f, 0.15f, 0.43f, 1.0f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.44f, 0.25f, 0.62f, 1.0f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.55f, 0.32f, 0.74f, 1.0f);
            colors[ImGuiCol_Header] = colors[ImGuiCol_Button];
            colors[ImGuiCol_HeaderHovered] = colors[ImGuiCol_ButtonHovered];
            colors[ImGuiCol_HeaderActive] = colors[ImGuiCol_ButtonActive];
            colors[ImGuiCol_CheckMark] = ImVec4(0.80f, 0.58f, 1.0f, 1.0f);
            colors[ImGuiCol_SliderGrab] = ImVec4(0.66f, 0.41f, 0.88f, 1.0f);
            colors[ImGuiCol_SliderGrabActive] = colors[ImGuiCol_CheckMark];
            colors[ImGuiCol_Separator] = colors[ImGuiCol_Border];
            colors[ImGuiCol_SeparatorHovered] = colors[ImGuiCol_ButtonHovered];
            colors[ImGuiCol_SeparatorActive] = colors[ImGuiCol_ButtonActive];
            colors[ImGuiCol_ScrollbarBg] = ImVec4(0.07f, 0.04f, 0.10f, 1.0f);
            colors[ImGuiCol_ScrollbarGrab] = colors[ImGuiCol_Button];
            colors[ImGuiCol_ScrollbarGrabHovered] = colors[ImGuiCol_ButtonHovered];
            colors[ImGuiCol_ScrollbarGrabActive] = colors[ImGuiCol_ButtonActive];
            colors[ImGuiCol_ResizeGrip] = ImVec4(0.66f, 0.41f, 0.88f, 0.35f);
            colors[ImGuiCol_ResizeGripHovered] = colors[ImGuiCol_ButtonHovered];
            colors[ImGuiCol_ResizeGripActive] = colors[ImGuiCol_ButtonActive];
            colors[ImGuiCol_NavHighlight] = colors[ImGuiCol_CheckMark];
            colors[ImGuiCol_TextSelectedBg] = ImVec4(0.55f, 0.32f, 0.74f, 0.50f);
            fonts::load();
            if (!ImGui_ImplWin32_Init(desc.OutputWindow))
            {
                ImGui::DestroyContext(gui);
                gui = nullptr;
                release(context);
                release(device);
                return false;
            }
            if (!ImGui_ImplDX11_Init(device, context))
            {
                ImGui_ImplWin32_Shutdown();
                ImGui::DestroyContext(gui);
                gui = nullptr;
                release(context);
                release(device);
                return false;
            }
            chain = candidate;
            chain->AddRef();
            window = desc.OutputWindow;
            SetLastError(0);
            previous_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(window_proc)));
            if (!previous_proc)
            {
                cleanup();
                return false;
            }
            // Pass-through observation of the game's render-target/depth sequence. Optional:
            // the overlay works without it (the depth probe then reads the view bound at Present).
            frame_trace::install(context);
            return true;
        }

        // Solo pause freezes the script VM clock (0x2DEA810, 50 ms ticks, the clock the
        // despawn countdown reads). No change for 200 ms means the pause menu is open.
        bool game_paused(std::uint64_t now)
        {
            static const bool vm = gsc_args::bindings_match();
            static std::uint32_t last_clock = 0;
            static std::uint64_t changed = 0;
            std::uint32_t clock = 0;
            if (!vm || !gsc_args::read(0x2DEA810, clock)) return false;
            if (clock != last_clock)
            {
                last_clock = clock;
                changed = now;
                return false;
            }
            return now - changed > 200;
        }

        // Scales the alpha of every vertex added to the list since index first.
        void fade(ImDrawList& list, int first, float strength)
        {
            for (int i = first; i < list.VtxBuffer.Size; ++i)
            {
                auto& color = list.VtxBuffer[i].col;
                const auto alpha =
                    static_cast<ImU32>(static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(strength, 0.0f, 1.0f));
                color = (color & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
            }
        }

        // FrameInfo for the injection's world drawing, from the injection camera.
        FrameInfo world_frame(const std::shared_ptr<const nav::Graph>& g, const nav::Camera& cam)
        {
            FrameInfo frame{};
            frame.graph = g;
            frame.camera = cam;
            frame.camera_valid = true;
            const auto now = GetTickCount64();
            frame.player_valid = settings.player_bounds && triggers::player_box(frame.player);
            frame.despawn_view = last_despawn_view;
            frame.spawn_snapshot = spawns::snapshot();
            frame.trigger_snapshot = triggers::snapshot();
            if (frame.trigger_snapshot && !triggers::fresh(*frame.trigger_snapshot, *g, now)) frame.trigger_snapshot.reset();
            const auto& s = frame.spawn_snapshot;
            frame.spawn_valid =
                s && now - s->updated <= 2000 && s->map->map == g->map && s->map->base == g->base && s->map->data == g->data;
            return frame;
        }

        // Runs inside the game's frame, right after its scene composite draw onto the
        // backbuffer and before any HUD draw (frame_trace). Builds the GPU batch (collision,
        // zombie hitboxes, NAV graph) and draws every other world overlay into a private ImGui
        // list, both from the current camera and tested against the scene depth, so zombies,
        // the weapon and props cover them. The HUD is drawn afterwards by the game.
        void inject_world()
        {
            std::lock_guard lock(guard);
            nav_injected = false;
            world_injected = false;
            const bool nav_wanted = settings.nav_nodes || settings.nav_links;
            if (stopping || !settings.visible || !gui || !device || !context || !target || !backbuffer_width || !backbuffer_height) return;
            const auto g = nav::graph();
            if (!g) return;
            nav::Camera cam{};
            if (!nav::read_camera(cam, *g)) return;
            const float sx = static_cast<float>(backbuffer_width) / static_cast<float>(cam.x + cam.width);
            const float sy = static_cast<float>(backbuffer_height) / static_cast<float>(cam.y + cam.height);
            collision_gpu::Batch batch;
            batch.viewport_x = cam.x * sx;
            batch.viewport_y = cam.y * sy;
            batch.viewport_width = cam.width * sx;
            batch.viewport_height = cam.height * sy;
            // Models on top: the model mask (written inside this frame by frame_trace) decides what
            // covers the collision (collision_gpu mode 1) instead of the box mask.
            const bool image_mask = settings.collision_in_frame && frame_trace::mask_written();
            if (settings.world_collision)
                if (const auto walls = collision::snapshot())
                {
                    std::vector<collision::Face> moving;
                    if (collision_moving) collision::movers(*walls, cam.origin, settings.collision_range, moving, collision_model_bounds);
                    build_collision(cam, *walls, moving, sx, sy, batch);
                }
            if (!collision_gpu::ready(device)) return;
            if (settings.zombie_boxes) build_zombie_boxes(cam, batch);
            // The NAV graph rides in the same pass so zombies and the weapon cover it. Draw()
            // skips its ImGui copy when this ran.
            if (nav_wanted)
            {
                build_nav(cam, *g, sx, sy, batch);
                nav_injected = true;
            }
            // Every other world drawing (spawns, triggers, aids, zombie markers, labels) goes
            // into a private ImGui draw list with this frame's camera and is drawn here too,
            // under the weapon and HUD (user request 2026-10-14). Present then skips it.
            const float width = static_cast<float>(backbuffer_width), height = static_cast<float>(backbuffer_height);
            auto* saved_context = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(gui);
            ImDrawList list(ImGui::GetDrawListSharedData());
            const bool fonts_ready = ImGui::GetIO().Fonts->TexID != ImTextureID{};
            if (fonts_ready)
            {
                list._ResetForNewFrame();
                list.PushTextureID(ImGui::GetIO().Fonts->TexID);
                list.PushClipRect(ImVec2(0, 0), ImVec2(width, height));
                FrameInfo frame = world_frame(g, cam);
                in_injection = true;
                draw(&list, width, height, frame, true, false);
                in_injection = false;
                if (frame_paused) fade(list, 0, paused_strength);
                world_spawn_count = frame.spawn_count;
            }
            ImGui::SetCurrentContext(saved_context);
            world_injected = fonts_ready;
            if (batch.empty() && list.VtxBuffer.empty()) return;
            ID3D11RenderTargetView* old[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
            ID3D11DepthStencilView* old_depth = nullptr;
            context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, old, &old_depth);
            auto* scene = frame_trace::scene_depth(); // always drawn under the HUD (user request 2026-10-13)
            // Weapon: anything within 1.5 near planes (the viewmodel reads about 1.015).
            collision_gpu::Occlusion occlusion;
            occlusion.scene = scene;
            occlusion.near_plane = scene_near;
            occlusion.weapon_distance = 1.5f * scene_near;
            const nav::Vec3* axes[] = {&cam.origin, &cam.forward, &cam.left, &cam.up};
            float* out[] = {occlusion.origin, occlusion.forward, occlusion.left, occlusion.up};
            for (int a = 0; a < 4; ++a)
            {
                out[a][0] = axes[a]->x;
                out[a][1] = axes[a]->y;
                out[a][2] = axes[a]->z;
            }
            occlusion.tan_x = cam.tan_x;
            occlusion.tan_y = cam.tan_y;
            // Only living zombies (box grown for arms and heads, swept over their trail) and
            // the weapon cover collision.
            const auto now = GetTickCount64();
            if (const auto z = zombies::snapshot(); z && now - z->updated <= 200)
                for (const auto& marker : z->markers)
                {
                    nav::Vec3 origin{}, mins{}, maxs{};
                    if (occlusion.box_count >= collision_gpu::Occlusion::max_boxes || !zombies::living_bounds(marker, origin, mins, maxs))
                        continue;
                    auto& t = trails[marker.number];
                    if (t.actor != marker.actor) t = Trail{marker.actor};
                    t.time[t.next] = now;
                    t.lo[t.next] = origin + mins;
                    t.hi[t.next] = origin + maxs;
                    t.next = (t.next + 1) % 32;
                    auto lo = origin + mins, hi = origin + maxs;
                    for (unsigned i = 0; i < 32; ++i)
                    {
                        if (!t.time[i] || now - t.time[i] > trail_ms) continue;
                        lo = {std::min(lo.x, t.lo[i].x), std::min(lo.y, t.lo[i].y), std::min(lo.z, t.lo[i].z)};
                        hi = {std::max(hi.x, t.hi[i].x), std::max(hi.y, t.hi[i].y), std::max(hi.z, t.hi[i].z)};
                    }
                    // A visibility mask, not the collision box: the entity bounds stop at 48 units
                    // (24 crawling) while heads reach about 70 and arms swing wide, so the bounds
                    // plus 12 cut heads off (live TranZit bounds and user screenshot, 2026-10-30).
                    auto& b = occlusion.boxes[occlusion.box_count++];
                    b[0] = lo.x - 21;
                    b[1] = lo.y - 21;
                    b[2] = lo.z - 4;
                    b[3] = hi.x + 21;
                    b[4] = hi.y + 21;
                    b[5] = std::max(hi.z + 40, origin.z + 64);
                }
            // 3D models cover collision too (user request 2026-10-30): the nearest prop, script
            // model and vehicle boxes fill the rest of the mask after the zombies.
            if (settings.world_collision)
                if (const auto walls = collision::snapshot())
                {
                    std::vector<collision::ModelBox> models;
                    collision::model_boxes(*walls, cam.origin, settings.collision_range,
                                           static_cast<std::size_t>(collision_gpu::Occlusion::max_boxes - occlusion.box_count), models);
                    for (const auto& m : models)
                    {
                        auto& b = occlusion.boxes[occlusion.box_count + occlusion.model_count++];
                        b[0] = m.lo.x - 2;
                        b[1] = m.lo.y - 2;
                        b[2] = m.lo.z - 2;
                        b[3] = m.hi.x + 2;
                        b[4] = m.hi.y + 2;
                        b[5] = m.hi.z + 2;
                    }
                }
            for (auto i = trails.begin(); i != trails.end();)
            {
                const auto& t = i->second;
                const auto newest = t.time[(t.next + 31) % 32];
                if (now - newest > 1000)
                    i = trails.erase(i);
                else
                    ++i;
            }
            if (!batch.empty())
            {
                auto collision_occlusion = occlusion;
                if (image_mask) collision_occlusion.mode = 1;
                collision_gpu::draw(device, context, target, backbuffer_width, backbuffer_height, batch,
                                    scene ? &collision_occlusion : nullptr);
            }
            if (!list.VtxBuffer.empty())
            {
                const float port[4] = {batch.viewport_x, batch.viewport_y, batch.viewport_width, batch.viewport_height};
                collision_gpu::draw_list(device, context, target, backbuffer_width, backbuffer_height, list, port,
                                         scene ? &occlusion : nullptr);
            }
            context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, old, old_depth);
            for (auto& t : old)
                release(t);
            release(old_depth);
            if (scene) scene->Release();
        }

        void render(IDXGISwapChain* candidate)
        {
            std::lock_guard lock(guard);
            if (stopping) return;
            auto saved = ImGui::GetCurrentContext();
            if (!chain && !attach(candidate))
            {
                ImGui::SetCurrentContext(saved);
                return;
            }
            if (candidate != chain)
            {
                ImGui::SetCurrentContext(saved);
                return;
            }
            ImGui::SetCurrentContext(gui);
            // The game's frame is complete here: close it for the frame trace before any overlay work.
            {
                DXGI_SWAP_CHAIN_DESC swap{};
                if (SUCCEEDED(candidate->GetDesc(&swap))) frame_trace::on_present(swap.BufferDesc.Width, swap.BufferDesc.Height);
            }
            if (!settings_loaded()) load_settings();
            // Unfocused (e.g. tabbed out of borderless fullscreen): keep drawing, but take no
            // input at all, so hotkeys and the menu cannot react to typing in other windows.
            const bool focused = GetForegroundWindow() == window;
            if (!focused)
            {
                menu_open = false;
                close_input();
                forget_keys();
            }
            if (moving_window)
            {
                ImGui::SetCurrentContext(saved);
                return;
            }
            if (focused)
            {
                // F8 toggles the menu. F9 closes it and stays with the timer's own menu hook.
                if (key_pressed(VK_F8)) menu_open = !menu_open;
                if (key_pressed(VK_F9) && menu_open) menu_open = false;
                handle_quick_keys(menu_open);
            }
            // Menu and hotkey changes reach the file within a second (and at shutdown).
            if (const auto now = GetTickCount64(); now - settings_checked >= 1000)
            {
                settings_checked = now;
                save_settings();
            }
            if (menu_open)
            {
                if (!clip_saved) clip_saved = GetClipCursor(&previous_clip) != FALSE;
                ClipCursor(nullptr);
                input_active = true;
            }
            else
                close_input();
            if (!settings.visible && !menu_open)
            {
                ImGui::SetCurrentContext(saved);
                return;
            }
            ID3D11Texture2D* buffer = nullptr;
            D3D11_TEXTURE2D_DESC desc{};
            if (FAILED(chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&buffer))))
            {
                close_input();
                ImGui::SetCurrentContext(saved);
                return;
            }
            buffer->GetDesc(&desc);
            if (!target) device->CreateRenderTargetView(buffer, nullptr, &target);
            // Next frame, the first game draw onto this backbuffer triggers inject_world.
            backbuffer_width = desc.Width;
            backbuffer_height = desc.Height;
            frame_trace::set_injection(settings.visible && !collision_gpu_failed ? &inject_world : nullptr, buffer);
            frame_trace::set_model_mask(settings.visible && settings.world_collision && settings.collision_in_frame &&
                                        !collision_gpu_failed);
            release(buffer);
            if (target && desc.Width && desc.Height)
            {
                ImGui::SetCurrentContext(gui);
                ImGui_ImplDX11_NewFrame();
                ImGui_ImplWin32_NewFrame();
                auto& io = ImGui::GetIO();
                const auto now = GetTickCount64();
                io.DeltaTime = last_frame ? std::max(0.001f, static_cast<float>(now - last_frame) / 1000) : 1.0f / 60;
                last_frame = now;
                io.DisplaySize = ImVec2(static_cast<float>(desc.Width), static_cast<float>(desc.Height));
                if (menu_open)
                {
                    POINT cursor{};
                    if (GetCursorPos(&cursor) && ScreenToClient(window, &cursor))
                        io.AddMousePosEvent(static_cast<float>(cursor.x), static_cast<float>(cursor.y));
                    const int buttons[] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
                    for (int i = 0; i < 5; ++i)
                        io.AddMouseButtonEvent(i, (GetAsyncKeyState(buttons[i]) & 0x8000) != 0);
                    if (wheel != 0)
                    {
                        io.AddMouseWheelEvent(0, wheel);
                        wheel = 0;
                    }
                }
                io.MouseDrawCursor = menu_open;
                ImGui::NewFrame();
                FrameInfo frame{};
                frame.graph = nav::graph();
                frame.camera_valid = frame.graph && nav::read_camera(frame.camera, *frame.graph);
                frame.player_valid = settings.player_bounds && frame.camera_valid && triggers::player_box(frame.player);
                frame.despawn_view = despawn::ui::capture(frame.graph);
                last_despawn_view = frame.despawn_view;
                frame.spawn_snapshot = spawns::snapshot();
                frame.trigger_snapshot = triggers::snapshot();
                if (frame.trigger_snapshot && (!frame.graph || !triggers::fresh(*frame.trigger_snapshot, *frame.graph, now)))
                    frame.trigger_snapshot.reset();
                const auto& s = frame.spawn_snapshot;
                frame.spawn_valid = s && frame.graph && now - s->updated <= 2000 && s->map->map == frame.graph->map &&
                                    s->map->base == frame.graph->base && s->map->data == frame.graph->data;
                if (settings.visible)
                {
                    auto* background = ImGui::GetBackgroundDrawList();
                    const int first = background->VtxBuffer.Size;
                    frame_paused = game_paused(now);
                    const bool world_done = frame_trace::injected_last_frame() && world_injected;
                    draw(background, io.DisplaySize.x, io.DisplaySize.y, frame, !world_done, true);
                    if (world_done) frame.spawn_count = world_spawn_count;
                    draw_movement_hud(io.DisplaySize.x, io.DisplaySize.y);
                    despawn::ui::hud(frame.despawn_view, io.DisplaySize.x, io.DisplaySize.y);
                    // Behind the semi-transparent pause menu: fade everything drawn this frame
                    // (the GPU collision batch is faded while it is built).
                    if (frame_paused) fade(*background, first, paused_strength);
                }
                draw_hint(now, io.DisplaySize.y);
                if (menu_open) menu(frame);
                if (!menu_open) close_input();
                ImGui::Render();
                ID3D11RenderTargetView* old[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
                ID3D11DepthStencilView* depth = nullptr;
                context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, old, &depth);
                frame_trace::overlay_begin();
                if (collision_gpu_pass && settings.visible)
                    collision_gpu::draw(device, context, target, desc.Width, desc.Height, collision_batch);
                context->OMSetRenderTargets(1, &target, nullptr);
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
                context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, old, depth);
                frame_trace::overlay_end();
                for (auto& t : old)
                    release(t);
                release(depth);
            }
            else
                close_input();
            ImGui::SetCurrentContext(saved);
        }

        HRESULT __stdcall present(IDXGISwapChain* candidate, UINT interval, UINT flags)
        {
            if (!(flags & DXGI_PRESENT_TEST))
            {
                try
                {
                    render(candidate);
                }
                catch (...)
                {
                    nav::clear_graph();
                }
            }
            return next_present(candidate, interval, flags);
        }

        HRESULT __stdcall resize(IDXGISwapChain* candidate, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
        {
            std::lock_guard lock(guard);
            if (candidate == chain) release(target);
            return next_resize(candidate, count, width, height, format, flags);
        }
    } // namespace

    bool startup()
    {
        // Retain this DLL until process exit: no unloading while graphics or SDK
        // callbacks may still be in flight. Normal restart is the supported removal.
        HMODULE self = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&startup),
                                &self))
            return false;
        const auto module = GetModuleHandleW(nullptr);
        WNDCLASSW wc{};
        wc.hInstance = module;
        wc.lpfnWndProc = DefWindowProcW;
        wc.lpszClassName = L"T6Gnosis_DXGI_Probe";
        if (!RegisterClassW(&wc)) return false;
        HWND probe = CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPED, 0, 0, 2, 2, nullptr, nullptr, module, nullptr);
        if (!probe)
        {
            UnregisterClassW(wc.lpszClassName, module);
            return false;
        }
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferCount = 1;
        desc.BufferDesc.Width = 2;
        desc.BufferDesc.Height = 2;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.OutputWindow = probe;
        desc.SampleDesc.Count = 1;
        desc.Windowed = TRUE;
        IDXGISwapChain* test = nullptr;
        ID3D11Device* test_device = nullptr;
        bool ok = false;
        if (SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &desc,
                                                    &test, &test_device, nullptr, nullptr)))
        {
            auto** table = *reinterpret_cast<void***>(test);
            present_address = table[8];
            resize_address = table[13];
            if (MH_Initialize() == MH_OK)
            {
                ok = MH_CreateHook(present_address, reinterpret_cast<void*>(present), reinterpret_cast<void**>(&next_present)) == MH_OK &&
                     MH_CreateHook(resize_address, reinterpret_cast<void*>(resize), reinterpret_cast<void**>(&next_resize)) == MH_OK;
                if (ok)
                    ok = MH_QueueEnableHook(present_address) == MH_OK && MH_QueueEnableHook(resize_address) == MH_OK &&
                         MH_ApplyQueued() == MH_OK;
                if (!ok)
                {
                    MH_DisableHook(present_address);
                    MH_DisableHook(resize_address);
                    MH_Uninitialize();
                }
            }
        }
        release(test);
        release(test_device);
        DestroyWindow(probe);
        UnregisterClassW(wc.lpszClassName, module);
        return ok;
    }

    void shutdown()
    {
        stopping = true;
        if (present_address) MH_DisableHook(present_address);
        if (resize_address) MH_DisableHook(resize_address);
        std::lock_guard lock(guard);
        auto saved = ImGui::GetCurrentContext();
        const bool ours = saved == gui;
        save_settings();
        cleanup();
        ImGui::SetCurrentContext(ours ? nullptr : saved);
    }
} // namespace overlay
