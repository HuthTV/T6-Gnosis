#pragma once
#include <Windows.h>
#include "jump_timing.hpp"
#include <plutonium_sdk.hpp>

// Movement HUD data (stamina bar, speedometer, jump stats). A MinHook detour on the
// engine's player-move function (PM_Move, prologue-checked before hooking) samples the
// local player's state each move. Reverse-engineered engine access, not an SDK API.
namespace movement
{
    struct Hud
    {
        bool visible = false, longer_sprint = false;
        float speed = 0, remaining = 0, capacity = 0, threshold = 1000;
        float jump_speed = -1, cancel_delta = 0;
        bool has_cancel_delta = false;
        int steps = -1, milliseconds = 0;
        ULONGLONG updated = 0;
    };

    // Latest sample, copied under the lock (render thread).
    Hud hud();
    // Installs the hook. Needs MinHook initialized (overlay::startup does that).
    void startup(plutonium::sdk::iinterface*);
    void shutdown();
} // namespace movement
