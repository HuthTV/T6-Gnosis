#define WIN32_LEAN_AND_MEAN
#include "practice.hpp"
#include "jump_timing.hpp"

#include <MinHook.h>

#include <cstring>

#include <cmath>

namespace practice
{
    namespace
    {
        using Move = void(__cdecl*)(unsigned char*);
        Move original=nullptr;

        constexpr uintptr_t address=0x944B30;
        SRWLOCK lock=SRWLOCK_INIT;

        bool installed=false;
        Hud display;
        const unsigned char* player=nullptr;
        int newest=-1;
        timing::Tracker tracker;
        struct Input
        {
            unsigned char* ps;
            int time, ground, flags, forward, old_forward;
            float speed;
            unsigned long long buttons, old_buttons;
        };
        int integer(const unsigned char* p,int offset) { return *reinterpret_cast<const int*>(p+offset); }
        int stamina(const unsigned char* ps)
        {
            return reinterpret_cast<int(__cdecl*)(const void*,int)>(0x468770)(ps,integer(ps,0));
        }
        bool read(unsigned char* pm,Input& s)
        {
            __try
            {
                const auto cg=*reinterpret_cast<const unsigned char* const*>(0x1113F9C);
                s.ps=*reinterpret_cast<unsigned char**>(pm);
                if (!cg || s.ps!=cg+0x480A8) return false;
                s.time=integer(pm,4);
                s.buttons=*reinterpret_cast<const unsigned long long*>(pm+8);
                s.old_buttons=*reinterpret_cast<const unsigned long long*>(pm+0x44);
                // Local t6zm.exe: PM_Move 0x944E34 compares signed cmd/oldcmd axes at +0x28/+0x64.
                s.forward=*reinterpret_cast<const signed char*>(pm+0x28);
                s.old_forward=*reinterpret_cast<const signed char*>(pm+0x64);
                s.ground=integer(s.ps,0xA4);
                s.flags=integer(s.ps,0xC);

                s.speed=std::hypot(*reinterpret_cast<const float*>(s.ps+0x34),*reinterpret_cast<const float*>(s.ps+0x38));
                return true;
            }
            __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        void reset(int)
        {

            AcquireSRWLockExclusive(&lock);
            player=nullptr; newest=-1; tracker.reset();

            display={};

            ReleaseSRWLockExclusive(&lock);
        }
        void __cdecl hook(unsigned char* pm)
        {
            Input input{};
            const bool local=read(pm,input);
            original(pm);
            if (!local) return;
            AcquireSRWLockExclusive(&lock);
            if (player!=input.ps)
            {
                player=input.ps; newest=-1; tracker.reset();
                display={};
            }
            if (input.time>=newest)
            {
                display.visible=integer(input.ps,4)==0;
                display.speed=std::hypot(*reinterpret_cast<const float*>(input.ps+0x34),*reinterpret_cast<const float*>(input.ps+0x38));
                display.capacity=static_cast<float>(reinterpret_cast<int(__cdecl*)(const void*)>(0x48C440)(input.ps));
                display.remaining=static_cast<float>(stamina(input.ps));
                display.threshold=reinterpret_cast<float(__cdecl*)()>(0x7306C0)()*1000.0f;
                display.longer_sprint=(integer(input.ps,0x54C)&32)!=0;
                display.updated=GetTickCount64();
            }
            if (input.time<=newest) { ReleaseSRWLockExclusive(&lock); return; }
            newest=input.time;
            const int ground_after=integer(input.ps,0xA4);
            const float vz=*reinterpret_cast<const float*>(input.ps+0x3C);
            const bool jumped=input.ground!=1023 && ground_after==1023 && vz>0 && (input.buttons & (1ull<<21));
            if (jumped)
            {
                display.jump_speed=display.speed;
                display.has_cancel_delta=false;
            }
            // Melee command bit 29: local meleebuttonpressed builtin 0x4316C6-0x4316DD.
            // ADS command bit 20: local adsbuttonpressed builtin 0x5F6436-0x5F644D.
            timing::Sample sample{input.time,input.buttons,input.old_buttons,input.forward,input.old_forward,
                (input.flags & 0x8000)!=0,jumped,input.speed};
            if (tracker.update(sample))
            {
                display.cancel_delta=display.speed-tracker.action_speed;
                display.has_cancel_delta=true;
            }
            display.has_cancel_delta=tracker.has_cancel_delta;
            display.steps=tracker.steps;
            display.milliseconds=tracker.milliseconds;
            display.action=tracker.result_action;
            ReleaseSRWLockExclusive(&lock);
        }
    }
    Hud hud()
    {
        AcquireSRWLockShared(&lock);
        const auto value=display;
        ReleaseSRWLockShared(&lock);
        return value;
    }
    void startup(plutonium::sdk::iinterface* api)
    {
        api->callbacks()->on_game_shutdown(reset);
        if (std::memcmp(reinterpret_cast<void*>(address),"\x55\x8B\xEC\x83\xE4\xF0",6)) return;
        installed=MH_CreateHook(reinterpret_cast<void*>(address),hook,reinterpret_cast<void**>(&original))==MH_OK;
        if (installed) installed=MH_EnableHook(reinterpret_cast<void*>(address))==MH_OK;
        if (!installed) api->logging()->info("[T6 Gnosis] Movement HUD unavailable on this game build.");
    }
    void shutdown()
    {
        if (installed)
        {
            MH_DisableHook(reinterpret_cast<void*>(address));
            MH_RemoveHook(reinterpret_cast<void*>(address));
        }
        reset(0);
    }
}

