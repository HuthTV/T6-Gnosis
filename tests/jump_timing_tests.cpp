#include "jump_timing.hpp"
#include <cstdlib>
#include <iostream>
#include <cstring>
using namespace practice::timing;
void check(bool ok) { if(!ok) { std::cerr<<"jump timing check failed\n"; std::exit(1); } }
int main()
{
    check(transitions(ads,0,127,127)==Ads);
    check(transitions(ads,ads,127,127)==0);
    check(transitions(0,ads,127,127)==0); // ADS release is not sprint cancellation.
    check(transitions(0,0,0,127)==ForwardRelease);
    check(transitions(0,0,-127,127)==ForwardRelease);
    check(transitions(0,0,0,-127)==0);
    check(transitions(0,0,0,0)==0);
    check(transitions(ads|fire|reload,0,0,127)==15);
    check(std::strcmp(action_label(12),"ADS+Forward release")==0);
    check(transitions(melee,0,127,127)==Melee);
    check(transitions(melee,melee,127,127)==0);
    check(transitions(0,melee,127,127)==0);
    check(transitions(melee|ads,0,0,127)==(Melee|Ads|ForwardRelease));
    check(std::strcmp(action_label(Melee),"Melee")==0);
    check(std::strcmp(action_label(31),"Fire+Reload+ADS+Forward release+Melee")==0);
    Tracker t;
    t.update({10,melee,0,127,127,true,false,300});
    t.update({20,melee,melee,127,127,true,false,290});
    t.update({30,jump|melee,melee,127,127,false,true,280});
    check(t.steps==2 && t.milliseconds==20 && t.result_action==Melee);
    t.reset();
    t.update({10,melee|ads|jump,0,0,127,true,true,300});
    check(t.steps==0 && t.result_action==(Melee|Ads|ForwardRelease));
    t.reset();
    t.update({0,melee,0,127,127,true,false,300});
    for(int i=1;i<20;++i) t.update({i*8,melee,melee,127,127,false,false,290});
    t.update({160,melee|jump,melee,127,127,false,true,280});
    check(t.steps==20 && t.milliseconds==160 && t.result_action==Melee);
    t.reset();
    t.update({0,melee,0,127,127,true,false,300});
    for(int i=1;i<=20;++i) t.update({i*8,melee,melee,127,127,false,false,290});
    t.update({168,melee|jump,melee,127,127,false,true,280});
    check(t.steps<0 && t.result_action==0 && !t.has_cancel_delta && t.action_step<0);
    t.reset();
    t.update({0,melee,0,127,127,false,false,300});
    t.update({8,jump,0,127,127,false,true,280});
    check(t.steps<0);
    t.reset();
    t.update({100,ads,0,127,127,true,false,300});
    t.update({108,ads,ads,127,127,false,false,290});
    t.update({108,ads|jump,ads,0,127,true,true,280}); // Duplicate cannot rearm or jump.
    check(t.steps<0 && t.sequence==2);
    check(t.update({116,ads|jump,ads,127,127,false,true,280}));
    check(t.steps==2 && t.milliseconds==16 && t.result_action==Ads && t.cancel_delta==-20);
    t.update({124,jump,jump,127,127,false,false,270});
    check(t.cancel_delta==-20);
    t.update({132,jump,0,127,127,false,true,260});
    check(!t.has_cancel_delta); // Unrelated later jump clears speed delta.
    t.reset();
    t.update({10,ads|jump,0,0,127,true,true,300});
    check(t.steps==0 && t.milliseconds==0 && t.result_action==12);
    t.reset();
    t.update({10,fire,0,127,127,true,false,300});
    t.update({20,reload,0,127,127,true,false,290});
    t.update({30,jump,0,127,127,false,true,280});
    check(t.steps==1 && t.milliseconds==10 && t.result_action==Reload);
    t.reset();
    t.update({10,0,0,0,127,false,false,300}); // Release outside sprint doesn't arm.
    t.update({20,jump,0,0,0,false,true,280});
    check(t.steps<0);
    t.reset();
    t.update({10,fire,0,127,127,true,false,300});
    t.update({2010,jump,0,127,127,false,false,280});
    check(t.steps==1 && t.milliseconds==2000);
    t.reset();
    t.update({10,fire,0,127,127,true,false,300});
    t.update({2011,jump,0,127,127,false,true,280});
    check(t.steps<0 && !t.has_cancel_delta);
    t.reset();
    check(t.steps<0 && t.newest==-1 && t.action_step==-1 && !t.has_cancel_delta);
    t.update({0,reload,0,127,127,true,false,300});
    t.update({8,jump,0,127,127,false,false,290});
    t.update({16,jump,jump,127,127,false,true,280});
    check(t.steps==1 && t.milliseconds==8 && t.result_action==Reload && t.has_cancel_delta);
    std::cout<<"Jump timing tests passed\n";
}
