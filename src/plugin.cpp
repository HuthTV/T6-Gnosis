#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <plutonium_sdk.hpp>
#include <algorithm>
#include <cstring>

namespace
{
    constexpr uintptr_t entities = 0x21C13C0;
    constexpr uintptr_t remaining_address = 0x468770;
    constexpr uintptr_t capacity_address = 0x48C440;
    constexpr uintptr_t threshold_address = 0x7306C0;
    constexpr uintptr_t add_vector_address = 0x4FAB00;
    using PlayerQuery = int(__cdecl*)(const void*);
    using RemainingQuery = int(__cdecl*)(const void*, int);
    using ThresholdQuery = float(__cdecl*)();
    using AddVector = void(__cdecl*)(int, const float*);

    bool bindings_match()
    {
        __try
        {
            return std::memcmp(reinterpret_cast<void*>(remaining_address), "\x56\x8B\x74\x24\x08\x57\x56", 7) == 0
                && std::memcmp(reinterpret_cast<void*>(capacity_address), "\x56\x8B\x74\x24\x08\xF7\x86", 7) == 0
                && std::memcmp(reinterpret_cast<void*>(threshold_address), "\xD9\xE8\xC3", 3) == 0
                && std::memcmp(reinterpret_cast<void*>(add_vector_address), "\x83\xEC\x0C\x8B\x44\x24\x14", 7) == 0
                && *reinterpret_cast<const float*>(0xBE41CC) == 1000.0f;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool sample(plutonium::sdk::types::entref entity, float* values)
    {
        if (entity.classnum != 0 || entity.entnum != 0) return false;
        __try
        {
            const auto ps = *reinterpret_cast<const unsigned char* const*>(entities + 0x154);
            if (!ps) return false;
            const int now = *reinterpret_cast<const int*>(ps);
            const int capacity = reinterpret_cast<PlayerQuery>(capacity_address)(ps);
            if (capacity <= 0 || capacity > 16383) return false;
            const int remaining = reinterpret_cast<RemainingQuery>(remaining_address)(ps, now);
            const float threshold = reinterpret_cast<ThresholdQuery>(threshold_address)()
                * *reinterpret_cast<const float*>(0xBE41CC);
            values[0] = static_cast<float>(std::clamp(remaining, 0, capacity));
            values[1] = static_cast<float>(capacity);
            values[2] = threshold;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    void stamina_sample(plutonium::sdk::types::entref entity)
    {
        float values[3] = {-1.0f, 0.0f, 0.0f};
        if (!sample(entity, values))
        {
            values[0] = -1.0f;
            values[1] = values[2] = 0.0f;
        }
        reinterpret_cast<AddVector>(add_vector_address)(0, values);
    }

    class stamina_plugin final : public plutonium::sdk::plugin
    {
        const char* plugin_name() override { return "T6 Stamina Viewer"; }
        bool is_game_supported(plutonium::sdk::game game) override
        {
            return game == plutonium::sdk::game::t6;
        }
        void on_startup(plutonium::sdk::iinterface* api, plutonium::sdk::game) override
        {
            if (!bindings_match())
            {
                api->logging()->info("[Stamina] Unsupported engine bindings; viewer disabled.");
                return;
            }
            api->gsc()->register_method("stamina_sample", stamina_sample);
            api->logging()->info("[Stamina] Registered stamina_sample; install stamina_hud.gsc for the HUD.");
        }
        void on_shutdown() override {}
    };
}

PLUTONIUM_API plutonium::sdk::plugin* on_initialize()
{
    static stamina_plugin instance;
    return &instance;
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
