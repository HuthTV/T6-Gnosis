#pragma once
#include <Windows.h>
#include "jump_timing.hpp"
#include <plutonium_sdk.hpp>
namespace practice
{
    struct Hud
    {
        bool visible=false, longer_sprint=false;
        float speed=0, remaining=0, capacity=0, threshold=1000;
        float jump_speed=-1, cancel_delta=0;
        bool has_cancel_delta=false;
        int steps=-1, milliseconds=0, action=0;
        ULONGLONG updated=0;
    };
    Hud hud();
    void startup(plutonium::sdk::iinterface*);
    void shutdown();



}

