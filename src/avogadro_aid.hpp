#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "navigation.hpp"

namespace plutonium::sdk::v1 {class iinterface;}
// TranZit Avogadro aid (user request 2026-10-30: his state, what he does next and how
// he will move). The GSC helper (gnosis_avogadro_aid) mirrors vanilla _zm_ai_avogadro
// with field reads and the queries its own think loop makes, and publishes:
//   GnosisAvogadroState( number, state, flags, hits, absorbed, phase_ms, region,
//       return_round, round, timer_ms )                         20 Hz while he exists
//   GnosisAvogadroAim( forward, eye, target_eye, distance, look )  20 Hz while he has an
//       enemy, or with the player as target while he waits to be seen
// No call takes more than 10 arguments, the most verified in game (GnosisWindAim).
//   GnosisAvogadroDash( index, end, clear )                     10 Hz while chasing
//   GnosisAvogadroBus( target, attack_ms, shock_ms )            20 Hz in the bus states
//   GnosisAvogadroSpawn( index, origin, candidate, region )     1 Hz, every avogadro_location
// His walking route is read natively (zombies::route), not published by the helper.
// Vanilla rules shown: chasing is ordinary zombie pathing (find_flesh); each 0.1 s think
// tick then tries the bus (enemy on it), a phase dash (2 s apart; one of 12 picked at
// random, taken only when maymovefrompointtopoint clears its end), then the bolt.
namespace avogadro {
// Vanilla self.state values, in the helper's numbering.
enum State : int {unknown,chamber,wait_for_player,idle,chasing,chasing_bus,attacking_bus,stay_attached,phasing,exiting,cloud};
// Helper flag bits (GNOSIS_AVO_* in the helper).
enum Flag : int {in_pain=1,shield=2,teleporting=4,traversing=8,enemy_on_bus=16,region_occupied=32,player_near=64,
    power_on=128,arriving=256,bolt_range=512,bolt_facing=1024,bolt_sight=2048,switches_on=4096,flag_mask=8191};
// Vanilla numbers (_zm_ai_avogadro.gsc): the bolt needs the enemy 120-600 units away
// (dist sq 14400-360000) within facing dot 0.99 (8.1 degrees) and a clear eye trace;
// facing him with dot 0.707 sends him from the chamber to the storm; he stays while a
// player is in his region or one not downed is within 3000; 4 melee hits defeat him and
// every 1000 other damage heals one.
inline constexpr float bolt_min=120,bolt_max=600,bolt_dot=0.99f,look_dot=0.707f,stay_radius=3000;
inline constexpr int defeat_hits=4,heal_damage=1000,dash_count=12,max_spawns=96;
// look and enemy_distance come from a fresh Aim sample (with_aim); -2 and -1 without one.
struct Status {
    int number=-1,state=unknown,flags=0,hits=0,phase_ms=0,region=-1,return_round=-1,round=0,timer_ms=-1;
    float absorbed=0,look=-2,enemy_distance=-1;std::uint64_t updated=0;
    bool has(int flag) const {return (flags&flag)!=0;}
    // Ghosted in the storm and not landing yet: there is no body to label.
    bool in_storm() const {return state==cloud&&!has(arriving);}
};
struct Aim {nav::Vec3 forward,eye,target_eye;float distance=-1,look=-2;std::uint64_t updated=0;};
struct Dash {nav::Vec3 end;bool clear=false;std::uint64_t updated=0;};
struct Bus {nav::Vec3 target;int attack_ms=-1,shock_ms=-1;std::uint64_t updated=0;};
struct Spawn {nav::Vec3 origin;bool candidate=false;int region=-1;std::uint64_t updated=0;};
struct View {Status status;Aim aim;std::array<Dash,dash_count> dashes{};Bus bus;std::array<Spawn,max_spawns> spawns{};};
inline constexpr std::uint64_t live_ms=250,dash_ms=400,spawn_ms=2500;
inline bool fresh(std::uint64_t updated,std::uint64_t now,std::uint64_t lifetime) {return updated&&now>=updated&&now-updated<=lifetime;}
inline Status with_aim(Status s,const Aim& aim,std::uint64_t now) {
    if(fresh(aim.updated,now,live_ms)) {s.look=aim.look;s.enemy_distance=aim.distance;}
    return s;
}
// Vanilla level.transit_region keys, in the helper's numbering.
inline const char* region_name(int region) {
    static constexpr const char* names[]={"the Bus Depot","the Diner","the Farm","the Cornfield","Power","Town"};
    return region>=0&&region<6?names[region]:"his region";
}
enum class Tone {title,plain,good,warn,bad,dim};
struct Line {std::string text;Tone tone=Tone::plain;};
// The lines over his head (or at the top of the screen while he is in the storm): state,
// hits and shield, then what he does next. clear is the number of dashes that fit now,
// -1 without a complete fresh dash sample.
inline std::vector<Line> describe(const Status& s,const Bus& bus,bool bus_fresh,int clear) {
    std::vector<Line> lines;char t[160]{};
    auto add=[&](Tone tone,const char* text) {lines.push_back({text,tone});};
    auto seconds=[](int ms) {return static_cast<double>(std::max(ms,0))/1000.0;};
    static constexpr const char* names[]={"Unknown","In the chamber","Waiting to be seen","Idle","Chasing","Heading for the bus",
        "Draining the bus","Riding the bus","Phasing","Leaving","In the storm"};
    const int state=s.state>=unknown&&s.state<=cloud?s.state:unknown;
    std::snprintf(t,sizeof(t),"AVOGADRO  %s%s",state==cloud&&s.has(arriving)?"Landed":names[state],s.has(in_pain)?" (in pain)":"");
    add(Tone::title,t);
    // Melee counts only with the shield down: from the bolt's fire notetrack to the end
    // of the bolt animation, and while he is on the bus.
    if(!s.in_storm()&&state!=chamber&&state!=wait_for_player) {
        if(!s.has(shield)) {std::snprintf(t,sizeof(t),"SHIELD DOWN: knife him  (hits %d/%d)",s.hits,defeat_hits);add(Tone::good,t);}
        else if(s.hits>0&&s.absorbed>0) {std::snprintf(t,sizeof(t),"Hits %d/%d  |  heals one at %.0f/%d damage  |  shield up",s.hits,defeat_hits,s.absorbed,heal_damage);add(Tone::plain,t);}
        else {std::snprintf(t,sizeof(t),"Hits %d/%d  |  shield up: knife does nothing",s.hits,defeat_hits);add(Tone::plain,t);}
    }
    switch(state) {
    case chamber:
        add(Tone::plain,s.has(power_on)?"Power is on: he wakes now":s.has(switches_on)?"Power rising: he wakes when it is on":"Wakes when the power is on");
        break;
    case wait_for_player:
        std::snprintf(t,sizeof(t),"Flees to the storm when you face him  (%.2f / %.2f)",s.look,look_dot);
        add(s.look>look_dot-0.15f?Tone::warn:Tone::plain,t);
        break;
    case idle:add(Tone::plain,"Starts chasing on the next tick");break;
    case chasing:
        if(s.has(enemy_on_bus))add(Tone::warn,"You are on the bus: he goes for it next");
        else if(s.phase_ms>0) {std::snprintf(t,sizeof(t),"Phase dash in %.1f s",seconds(s.phase_ms));add(Tone::plain,t);}
        else if(s.has(traversing))add(Tone::plain,"Phase dash waits for the traversal");
        else if(clear>0) {std::snprintf(t,sizeof(t),"Phase dash ready: %d/%d dashes fit",clear,dash_count);add(Tone::warn,t);}
        else if(clear==0)add(Tone::plain,"Phase dash ready, but no dash fits");
        else add(Tone::plain,"Phase dash ready");
        if(s.has(bolt_range)&&s.has(bolt_facing)&&s.has(bolt_sight))add(Tone::bad,"BOLT READY: he can shoot you now");
        else if(s.enemy_distance>=0) {
            std::snprintf(t,sizeof(t),"Bolt: range %s (%.0f)  facing %s  sight %s",s.has(bolt_range)?"ok":"no",s.enemy_distance,
                s.has(bolt_facing)?"ok":"no",!s.has(bolt_range)?"-":s.has(bolt_sight)?"ok":"no");
            add(Tone::dim,t);
        }
        // region_empty, checked every 0.5 s.
        if(!s.has(region_occupied)&&!s.has(player_near)) {
            std::snprintf(t,sizeof(t),"LEAVES at the next check: nobody in %s or within %.0f",region_name(s.region),stay_radius);add(Tone::warn,t);
        }
        break;
    case chasing_bus:add(Tone::warn,"Drains the bus once within 120 units of it");break;
    case attacking_bus:
        if(bus_fresh&&bus.attack_ms>=0) {std::snprintf(t,sizeof(t),"Bus stops in %.1f s  |  shock in %.1f s",seconds(bus.attack_ms),seconds(bus.shock_ms));add(Tone::bad,t);}
        else add(Tone::bad,"Draining the bus: it stops after 20 s");
        break;
    case stay_attached:
        if(bus_fresh&&bus.shock_ms>=0) {std::snprintf(t,sizeof(t),"Shocks riders every 2 s  |  next in %.1f s",seconds(bus.shock_ms));add(Tone::bad,t);}
        add(Tone::plain,"Lets go once nobody rides the bus");
        break;
    case phasing:add(Tone::plain,"Dashing: immune");break;
    case exiting:add(Tone::plain,"Leaving for the storm: immune");break;
    case cloud:
        if(s.has(arriving)) {
            if(s.timer_ms>=0)std::snprintf(t,sizeof(t),"Leaves in ~%.0f s unless you enter %s",seconds(s.timer_ms),region_name(s.region));
            else std::snprintf(t,sizeof(t),"Leaves 30 s after landing unless you enter %s",region_name(s.region));
            add(s.has(region_occupied)?Tone::plain:Tone::warn,t);
        } else {
            if(s.timer_ms>=0)std::snprintf(t,sizeof(t),"Storm over %s  |  region roll in ~%.0f s",region_name(s.region),seconds(s.timer_ms));
            else std::snprintf(t,sizeof(t),"Storm over %s",region_name(s.region));
            add(Tone::plain,t);
            if(s.return_round>=0) {
                if(s.round>=s.return_round)add(Tone::warn,"Lands now");
                else {std::snprintf(t,sizeof(t),"Lands in round %d  (now %d)",s.return_round,s.round);add(Tone::plain,t);}
            }
        }
        break;
    }
    return lines;
}
// v1::iinterface is the pinned SDK's plutonium::sdk::iinterface alias.
bool startup(plutonium::sdk::v1::iinterface* api);
void clear();
View snapshot();
}
