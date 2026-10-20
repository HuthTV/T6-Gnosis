#pragma once
#include <cstdint>
namespace practice::timing
{
    constexpr std::uint64_t fire=1ull<<31, reload=1ull<<27, ads=1ull<<20, jump=1ull<<21, melee=1ull<<29;
    constexpr int max_updates=20;
    enum Action { Fire=1, Reload=2, Ads=4, ForwardRelease=8, Melee=16 };
    inline int transitions(std::uint64_t buttons, std::uint64_t old_buttons, int forward, int old_forward)
    {
        const auto pressed=buttons & ~old_buttons;
        return ((pressed&fire)?Fire:0) | ((pressed&reload)?Reload:0) |
            ((pressed&ads)?Ads:0) | ((pressed&melee)?Melee:0) | ((old_forward>0 && forward<=0)?ForwardRelease:0);
    }
    inline const char* action_label(int action)
    {
        static constexpr const char* labels[]={"", "Fire", "Reload", "Fire+Reload", "ADS", "Fire+ADS",
            "Reload+ADS", "Fire+Reload+ADS", "Forward release", "Fire+Forward release",
            "Reload+Forward release", "Fire+Reload+Forward release", "ADS+Forward release",
            "Fire+ADS+Forward release", "Reload+ADS+Forward release", "Fire+Reload+ADS+Forward release",
            "Melee", "Fire+Melee", "Reload+Melee", "Fire+Reload+Melee", "ADS+Melee", "Fire+ADS+Melee",
            "Reload+ADS+Melee", "Fire+Reload+ADS+Melee", "Forward release+Melee", "Fire+Forward release+Melee",
            "Reload+Forward release+Melee", "Fire+Reload+Forward release+Melee", "ADS+Forward release+Melee",
            "Fire+ADS+Forward release+Melee", "Reload+ADS+Forward release+Melee", "Fire+Reload+ADS+Forward release+Melee"};
        return action>0 && action<32 ? labels[action] : "";
    }
    struct Sample { int time=0; std::uint64_t buttons=0, old_buttons=0; int forward=0, old_forward=0; bool sprinting=false, jumped=false; float speed=0; };
    struct Tracker
    {
        int newest=-1, sequence=0, action_step=-1, action_time=0, action=0;
        int steps=-1, milliseconds=0, result_action=0;
        float action_speed=0, cancel_delta=0;
        bool has_cancel_delta=false;
        void reset() { *this={}; }
        bool update(const Sample& s)
        {
            if(s.time<=newest) return false; // Prediction replays/render calls are not additional updates.
            newest=s.time; ++sequence;
            if(s.jumped) has_cancel_delta=false;
            const int event=transitions(s.buttons,s.old_buttons,s.forward,s.old_forward);
            if(event && s.sprinting)
            {
                // Latest eligible action wins; simultaneous actions retain every identity.
                action_step=sequence; action_time=s.time; action=event; action_speed=s.speed;
                steps=-1; milliseconds=0; result_action=0; has_cancel_delta=false;
            }
            if(action_step>=0 && (s.time-action_time>2000 ||
                (steps<0 && sequence-action_step>max_updates))) action_step=-1;
            if(action_step<0) return false;
            if((s.buttons & ~s.old_buttons & jump) && steps<0)
            {
                steps=sequence-action_step; milliseconds=s.time-action_time; result_action=action;
            }
            if(s.jumped && steps>=0)
            {
                cancel_delta=s.speed-action_speed; has_cancel_delta=true; action_step=-1; return true;
            }
            return false;
        }
    };
}
