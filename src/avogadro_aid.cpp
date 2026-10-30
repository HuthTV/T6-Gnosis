#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <plutonium_sdk.hpp>
#include "avogadro_aid.hpp"
#include <atomic>
#include <mutex>
#include "gsc_args.hpp"

namespace avogadro {
namespace {
std::mutex guard;
std::atomic<bool> ready{false};
View current;
bool unit(nav::Vec3 v) {const float l=nav::dot(v,v);return l>0.81f&&l<1.21f;}
void state_impl() {
    gsc_args::Arguments args;Status s;
    if(!args.capture(10)||!args.integer(0,s.number)||!args.integer(1,s.state)||!args.integer(2,s.flags)||!args.integer(3,s.hits)||
       !args.number(4,s.absorbed)||!args.integer(5,s.phase_ms)||!args.integer(6,s.region)||!args.integer(7,s.return_round)||
       !args.integer(8,s.round)||!args.integer(9,s.timer_ms)||!args.unchanged())return;
    if(s.number<0||s.number>=1024||s.state<unknown||s.state>cloud||s.flags<0||(s.flags&~flag_mask)||s.hits<0||s.hits>16||
       s.absorbed<0||s.absorbed>100000||s.region< -1||s.region>5||s.return_round< -1||s.return_round>10000||s.round<0||s.round>10000)return;
    // phase_time starts at 0, so early in a game the cooldown is far in the past.
    s.phase_ms=std::clamp(s.phase_ms,-100000,100000);s.timer_ms=std::clamp(s.timer_ms,-1,600000);
    s.updated=GetTickCount64();
    std::lock_guard lock(guard);current.status=s;
}
void aim_impl() {
    gsc_args::Arguments args;Aim aim;
    if(!args.capture(5)||!args.vector(0,aim.forward)||!args.vector(1,aim.eye)||!args.vector(2,aim.target_eye)||!args.number(3,aim.distance)||
       !args.number(4,aim.look)||!args.unchanged()||!unit(aim.forward)||aim.distance<0||aim.distance>1000000||aim.look< -2||aim.look>1.01f)return;
    aim.updated=GetTickCount64();
    std::lock_guard lock(guard);current.aim=aim;
}
void dash_impl() {
    gsc_args::Arguments args;int index=0,clear=0;Dash dash;
    if(!args.capture(3)||!args.integer(0,index)||!args.vector(1,dash.end)||!args.integer(2,clear)||!args.unchanged())return;
    if(index<0||index>=dash_count||clear<0||clear>1)return;
    dash.clear=clear==1;dash.updated=GetTickCount64();
    std::lock_guard lock(guard);current.dashes[static_cast<std::size_t>(index)]=dash;
}
void bus_impl() {
    gsc_args::Arguments args;Bus bus;
    if(!args.capture(3)||!args.vector(0,bus.target)||!args.integer(1,bus.attack_ms)||!args.integer(2,bus.shock_ms)||!args.unchanged())return;
    if(bus.attack_ms< -1||bus.attack_ms>20000||bus.shock_ms< -1||bus.shock_ms>2000)return;
    bus.updated=GetTickCount64();
    std::lock_guard lock(guard);current.bus=bus;
}
void spawn_impl() {
    gsc_args::Arguments args;int index=0,candidate=0;Spawn spawn;
    if(!args.capture(4)||!args.integer(0,index)||!args.vector(1,spawn.origin)||!args.integer(2,candidate)||!args.integer(3,spawn.region)||!args.unchanged())return;
    if(index<0||index>=max_spawns||candidate<0||candidate>1||spawn.region< -1||spawn.region>5)return;
    spawn.candidate=candidate==1;spawn.updated=GetTickCount64();
    std::lock_guard lock(guard);current.spawns[static_cast<std::size_t>(index)]=spawn;
}
template<void(*Callback)()> void invoke() noexcept {
    if(!ready.load())return;
    try {Callback();}catch(...) {clear();}
}
void PLUTONIUM_CALLBACK state_callback() {invoke<state_impl>();}
void PLUTONIUM_CALLBACK aim_callback() {invoke<aim_impl>();}
void PLUTONIUM_CALLBACK dash_callback() {invoke<dash_impl>();}
void PLUTONIUM_CALLBACK bus_callback() {invoke<bus_impl>();}
void PLUTONIUM_CALLBACK spawn_callback() {invoke<spawn_impl>();}
}
bool startup(plutonium::sdk::v1::iinterface* api) {
    ready=false;clear();if(!api)return false;
    // Registered before the check, as in mob_aid; mismatched builds ignore the calls.
    api->gsc()->register_function("GnosisAvogadroState",state_callback);
    api->gsc()->register_function("GnosisAvogadroAim",aim_callback);
    api->gsc()->register_function("GnosisAvogadroDash",dash_callback);
    api->gsc()->register_function("GnosisAvogadroBus",bus_callback);
    api->gsc()->register_function("GnosisAvogadroSpawn",spawn_callback);
    ready=gsc_args::bindings_match();
    return ready;
}
void clear() {std::lock_guard lock(guard);current=View{};}
View snapshot() {std::lock_guard lock(guard);return current;}
}
