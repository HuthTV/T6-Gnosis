// Screen-space HUD drawings: the start-up hint and the movement HUD (stamina bar,
// speedometer, jump stats).
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "overlay_shared.hpp"
#include "fonts.hpp"
#include "movement.hpp"

namespace overlay
{
    namespace
    {
        // Start-up hint (user requests 2026-10-20): a few seconds after the overlay first draws,
        // a small top-left banner names the menu key for 10 s, its key cap brightening slowly
        // (4 s cycle), then fades out. Once per launch. Opening the menu ends it.
        constexpr std::uint64_t hint_delay_ms = 3000, hint_show_ms = 10000, hint_fade_ms = 1000;
        std::uint64_t first_frame_at = 0;
        bool hint_done = false;
    } // namespace

    void draw_hint(std::uint64_t now, float height)
    {
        if (!first_frame_at) first_frame_at = now;
        if (menu_open) hint_done = true;
        const auto age = now - first_frame_at;
        if (hint_done || age < hint_delay_ms) return;
        const auto shown = age - hint_delay_ms;
        if (shown > hint_show_ms)
        {
            hint_done = true;
            return;
        }
        const float fade_in = std::min(1.0f, static_cast<float>(shown) / 250.0f);
        const float fade_out = std::min(1.0f, static_cast<float>(hint_show_ms - shown) / static_cast<float>(hint_fade_ms));
        const float alpha = std::clamp(std::min(fade_in, fade_out), 0.0f, 1.0f);
        const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(shown) * 6.2831853f / 4000.0f); // one slow cycle per 4 s
        const float scale = height / 1080.0f, size = 22.0f * scale, pad = 8 * scale;
        auto* font = fonts::get(size);
        auto* list = ImGui::GetForegroundDrawList();
        const char* key = "F8";
        const char* message = "opens the T6 Gnosis menu";
        const auto key_size = font->CalcTextSizeA(size, 10000, 0, key), text_size = font->CalcTextSizeA(size, 10000, 0, message);
        const float cap_width = key_size.x + pad * 2, total = cap_width + pad + text_size.x;
        const ImVec2 top(24 * scale, 24 * scale);
        auto a = [&](float value) { return static_cast<int>(value * alpha); };
        // Banner plate, then the key cap (pulsing purple to light purple) and the message.
        list->AddRectFilled(ImVec2(top.x - pad * 1.5f, top.y - pad), ImVec2(top.x + total + pad * 1.5f, top.y + key_size.y + pad),
                            IM_COL32(19, 11, 29, a(230)), 6 * scale);
        list->AddRect(ImVec2(top.x - pad * 1.5f, top.y - pad), ImVec2(top.x + total + pad * 1.5f, top.y + key_size.y + pad),
                      IM_COL32(102, 64, 145, a(200)), 6 * scale, 0, 1.5f * scale);
        const int r = static_cast<int>(110 + 90 * pulse), g = static_cast<int>(60 + 80 * pulse), b = static_cast<int>(170 + 70 * pulse);
        list->AddRectFilled(ImVec2(top.x, top.y - pad * 0.5f), ImVec2(top.x + cap_width, top.y + key_size.y + pad * 0.5f),
                            IM_COL32(r, g, b, a(255)), 4 * scale);
        list->AddText(font, size, ImVec2(top.x + pad, top.y), IM_COL32(255, 255, 255, a(255)), key);
        list->AddText(font, size, ImVec2(top.x + cap_width + pad + scale, top.y + scale), IM_COL32(0, 0, 0, a(200)), message);
        list->AddText(font, size, ImVec2(top.x + cap_width + pad, top.y), IM_COL32(240, 236, 250, a(255)), message);
    }

    // Movement HUD under the crosshair area: stamina bar, speedometer and jump stats.
    void draw_movement_hud(float width, float height)
    {
        const auto s = movement::hud();
        if (!s.visible || GetTickCount64() - s.updated > 1000 || !std::isfinite(s.speed) || s.capacity <= 0) return;
        const float scale = height / 1080.0f;
        // Bar 206 units above the bottom (was 146): the bottom row, jump speed, now ends
        // near y 960 of 1080, clear of the power-up icons from about 1030 (user
        // screenshot 2026-10-13: the jump-speed row covered the Insta-Kill skull).
        const float cx = width * 0.5f, y = height - 206.0f * scale;
        const float half = 67.5f * scale, thick = 4.5f * scale, font_size = 29.25f * scale; // 39 until 2026-10-13 (user: 25% smaller)
        auto* list = ImGui::GetBackgroundDrawList();
        if (settings.stamina_bar)
        {
            list->AddRectFilled(ImVec2(cx - half, y - thick * 0.5f), ImVec2(cx + half, y + thick * 0.5f), IM_COL32(25, 25, 25, 210));
            const float fraction = std::clamp(s.remaining / s.capacity, 0.0f, 1.0f);
            const auto bar_color = s.remaining > s.threshold - 100 ? IM_COL32(64, 217, 115, 255) : IM_COL32(255, 140, 51, 255);
            if (fraction > 0)
                list->AddRectFilled(ImVec2(cx - half, y - thick * 0.5f), ImVec2(cx - half + 2 * half * fraction, y + thick * 0.5f),
                                    bar_color);
        }
        auto text = [&](const char* value, float top, ImU32 color, float font_scale = 1.0f) {
            const float actual_font_size = font_size * font_scale;
            const auto size = fonts::get(actual_font_size)->CalcTextSizeA(actual_font_size, 10000, 0, value);
            const ImVec2 at(cx - size.x * 0.5f, top);
            list->AddText(fonts::get(actual_font_size), actual_font_size, ImVec2(at.x + scale, at.y + scale), IM_COL32(0, 0, 0, 200),
                          value);
            list->AddText(fonts::get(actual_font_size), actual_font_size, at, color, value);
        };
        char label[96]{};
        if (settings.jump_stats && s.steps >= 0)
        {
            std::snprintf(label, sizeof(label), "%d frame %dms", s.steps, s.milliseconds);
            text(label, y - thick * 0.5f - 4 * scale - font_size, IM_COL32(240, 240, 240, 255));
        }
        if (settings.speedometer)
        {
            const float min_speed = s.longer_sprint ? 310.0f : 260.0f;
            const float max_speed = s.longer_sprint ? 390.0f : 350.0f;
            const float ratio = std::clamp((std::floor(s.speed) - min_speed) / (max_speed - min_speed), 0.0f, 1.0f);
            const float t = ratio < 0.5f ? ratio * 2 : (ratio - 0.5f) * 2;
            const ImVec4 color = ratio < 0.5f ? ImVec4(0.6f + 0.4f * t, 1, 0.6f, 1) : ImVec4(1, 1 - 0.8f * t, 0.6f - 0.6f * t, 1);
            std::snprintf(label, sizeof(label), "%d", static_cast<int>(s.speed));
            text(label, y + thick * 0.5f + 4 * scale, ImGui::ColorConvertFloat4ToU32(color));
        }
        if (settings.jump_stats && s.jump_speed >= 0)
        {
            if (s.has_cancel_delta)
                std::snprintf(label, sizeof(label), "%.0f (%+.0f)", std::floor(s.jump_speed), std::round(s.cancel_delta));
            else
                std::snprintf(label, sizeof(label), "%.0f", std::floor(s.jump_speed));
            text(label, y + thick * 0.5f + 8 * scale + font_size, IM_COL32(220, 225, 230, 255));
        }
    }
} // namespace overlay
