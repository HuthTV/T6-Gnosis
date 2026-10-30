#include "avogadro_aid.hpp"
#include <cstdio>
#include <cstring>

namespace {
int failures=0;
void check(bool good,const char* why) {if(!good) {++failures;std::printf("FAIL: %s\n",why);}}
bool has(const std::vector<avogadro::Line>& lines,const char* text,avogadro::Tone tone) {
    for(const auto& l:lines)if(l.tone==tone&&l.text.find(text)!=std::string::npos)return true;
    return false;
}
bool mentions(const std::vector<avogadro::Line>& lines,const char* text) {
    for(const auto& l:lines)if(l.text.find(text)!=std::string::npos)return true;
    return false;
}
avogadro::Status chasing() {
    avogadro::Status s;s.number=22;s.state=avogadro::chasing;s.region=2;s.updated=1;
    s.flags=avogadro::shield|avogadro::region_occupied|avogadro::player_near;s.enemy_distance=800;
    return s;
}
}
int main() {
    using avogadro::Tone;const avogadro::Bus no_bus;
    auto s=chasing();s.phase_ms=1300;
    auto lines=avogadro::describe(s,no_bus,false,-1);
    check(lines.size()>=3&&lines[0].tone==Tone::title&&lines[0].text=="AVOGADRO  Chasing","title names the state");
    check(has(lines,"Phase dash in 1.3 s",Tone::plain),"dash cooldown shown");
    check(has(lines,"shield up: knife does nothing",Tone::plain),"shield up without hits");
    check(has(lines,"Bolt: range no (800)  facing no  sight -",Tone::dim),"bolt conditions listed, sight unknown out of range");
    check(!mentions(lines,"LEAVES"),"no leave warning while the region is occupied");
    s.phase_ms=-5;lines=avogadro::describe(s,no_bus,false,7);
    check(has(lines,"Phase dash ready: 7/12 dashes fit",Tone::warn),"ready dashes counted");
    lines=avogadro::describe(s,no_bus,false,0);
    check(has(lines,"no dash fits",Tone::plain),"ready without a fitting dash");
    s.flags|=avogadro::traversing;lines=avogadro::describe(s,no_bus,false,5);
    check(has(lines,"waits for the traversal",Tone::plain),"traversal delays the dash");
    s=chasing();s.flags|=avogadro::bolt_range|avogadro::bolt_facing|avogadro::bolt_sight;
    check(has(avogadro::describe(s,no_bus,false,-1),"BOLT READY",Tone::bad),"bolt ready when all three conditions hold");
    s=chasing();s.flags&=~avogadro::shield;s.hits=2;
    check(has(avogadro::describe(s,no_bus,false,-1),"SHIELD DOWN: knife him  (hits 2/4)",Tone::good),"shield down invites the knife");
    s=chasing();s.hits=1;s.absorbed=350;
    check(has(avogadro::describe(s,no_bus,false,-1),"heals one at 350/1000 damage",Tone::plain),"healing progress while wounded");
    s=chasing();s.flags&=~(avogadro::region_occupied|avogadro::player_near);
    check(has(avogadro::describe(s,no_bus,false,-1),"LEAVES at the next check: nobody in the Farm or within 3000",Tone::warn),"empty region warning");
    s=chasing();s.flags|=avogadro::enemy_on_bus;
    check(has(avogadro::describe(s,no_bus,false,-1),"goes for it next",Tone::warn),"bus has priority");

    avogadro::Status storm;storm.state=avogadro::cloud;storm.region=4;storm.timer_ms=12400;storm.return_round=7;storm.round=5;
    lines=avogadro::describe(storm,no_bus,false,-1);
    check(storm.in_storm()&&lines[0].text=="AVOGADRO  In the storm","storm title");
    check(!mentions(lines,"Hits"),"no hits line in the storm");
    check(has(lines,"Storm over Power  |  region roll in ~12 s",Tone::plain),"storm region and roll");
    check(has(lines,"Lands in round 7  (now 5)",Tone::plain),"return round");
    storm.round=7;check(has(avogadro::describe(storm,no_bus,false,-1),"Lands now",Tone::warn),"lands once the round is reached");
    storm.flags=avogadro::arriving|avogadro::shield;storm.timer_ms=18000;
    lines=avogadro::describe(storm,no_bus,false,-1);
    check(!storm.in_storm()&&lines[0].text=="AVOGADRO  Landed","landed title");
    check(has(lines,"Leaves in ~18 s unless you enter Power",Tone::warn),"leave countdown after landing");
    storm.timer_ms=-1;check(has(avogadro::describe(storm,no_bus,false,-1),"Leaves 30 s after landing",Tone::warn),"leave rule without the animation length");

    avogadro::Status chamber;chamber.state=avogadro::chamber;
    lines=avogadro::describe(chamber,no_bus,false,-1);
    check(lines.size()==2&&has(lines,"Wakes when the power is on",Tone::plain),"chamber waits for power without a hits line");
    chamber.state=avogadro::wait_for_player;chamber.look=0.65f;
    check(has(avogadro::describe(chamber,no_bus,false,-1),"Flees to the storm when you face him  (0.65 / 0.71)",Tone::warn),"look threshold close");

    avogadro::Status bus;bus.state=avogadro::attacking_bus;bus.flags=0;
    avogadro::Bus drain;drain.attack_ms=12300;drain.shock_ms=400;
    lines=avogadro::describe(bus,drain,true,-1);
    check(has(lines,"Bus stops in 12.3 s  |  shock in 0.4 s",Tone::bad),"bus drain countdown");
    check(has(lines,"SHIELD DOWN",Tone::good),"on the bus the shield is down");
    check(has(avogadro::describe(bus,drain,false,-1),"it stops after 20 s",Tone::bad),"stale bus sample falls back to the rule");

    avogadro::Status odd;odd.state=99;
    check(avogadro::describe(odd,no_bus,false,-1)[0].text=="AVOGADRO  Unknown","unknown state is not indexed out of range");
    check(std::strcmp(avogadro::region_name(-1),"his region")==0&&std::strcmp(avogadro::region_name(6),"his region")==0,"region bounds");
    check(avogadro::fresh(100,300,250)&&!avogadro::fresh(100,400,250)&&!avogadro::fresh(0,10,250)&&!avogadro::fresh(500,400,250),"freshness window");
    avogadro::Aim aim;aim.distance=340;aim.look=0.5f;aim.updated=1000;
    const auto merged=avogadro::with_aim(chasing(),aim,1100),stale=avogadro::with_aim(chasing(),aim,2000);
    check(merged.enemy_distance==340&&merged.look==0.5f,"fresh aim supplies distance and look");
    check(stale.enemy_distance==800&&stale.look==-2,"stale aim leaves the status as published");
    std::printf("Avogadro aid checks: %s (%d failures)\n",failures?"FAIL":"PASS",failures);return failures?1:0;
}
