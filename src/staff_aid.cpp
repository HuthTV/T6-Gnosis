#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "staff_aid.hpp"
#include <atomic>
#include <mutex>
#include "gsc_args.hpp"

namespace staff {
namespace {
std::mutex guard;
std::atomic<bool> ready{false};
View current;
bool ordered(nav::Vec3 lo,nav::Vec3 hi) {
    // Detector brushes are about 26x26x110; anything far larger is not one.
    return lo.x<=hi.x&&lo.y<=hi.y&&lo.z<=hi.z&&hi.x-lo.x<=512&&hi.y-lo.y<=512&&hi.z-lo.z<=512;
}
void smoke_impl() {
    gsc_args::Arguments args;int index=0,solved=0,cooling=0;nav::Vec3 lo{},hi{},to_dest{};
    if(!args.capture(6)||!args.integer(0,index)||!args.vector(1,lo)||!args.vector(2,hi)||!args.vector(3,to_dest)||
       !args.integer(4,solved)||!args.integer(5,cooling)||!args.unchanged())return;
    if(index<0||index>=static_cast<int>(current.smokes.size())||!ordered(lo,hi)||solved<0||solved>1||cooling<0||cooling>1)return;
    if(std::abs(nav::dot(to_dest,to_dest)-1)>0.01f)return;
    std::lock_guard lock(guard);
    auto& s=current.smokes[static_cast<std::size_t>(index)];
    s.mins=lo;s.maxs=hi;s.to_dest=to_dest;s.solved=solved==1;s.cooling=cooling==1;s.updated=GetTickCount64();
}
void aim_impl() {
    gsc_args::Arguments args;int verdict=0,hit=0,smoke=0;Aim aim;
    if(!args.capture(10)||!args.integer(0,verdict)||!args.integer(1,hit)||!args.integer(2,smoke)||!args.vector(3,aim.point)||
       !args.vector(4,aim.forward)||!args.vector(5,aim.direction)||!args.number(6,aim.threshold)||!args.number(7,aim.cooldown_ms)||
       !args.number(8,aim.world_dist)||!args.number(9,aim.brush_dist)||!args.unchanged())return;
    if(verdict<0||verdict>3||hit<0||hit>2||smoke< -1||smoke>=static_cast<int>(current.smokes.size()))return;
    if(std::abs(nav::dot(aim.forward,aim.forward)-1)>0.01f||aim.threshold< -1||aim.threshold>1||aim.cooldown_ms<0)return;
    aim.verdict=static_cast<Verdict>(verdict);aim.hit=static_cast<Hit>(hit);aim.smoke=smoke;aim.updated=GetTickCount64();
    std::lock_guard lock(guard);
    current.aim=aim;
}
void torch_impl() {
    gsc_args::Arguments args;int index=0,correct=0,lit=0;Torch torch;
    if(!args.capture(6)||!args.integer(0,index)||!args.vector(1,torch.origin)||!args.integer(2,correct)||!args.integer(3,lit)||
       !args.number(4,torch.lit_left_ms)||!args.number(5,torch.rearm_ms)||!args.unchanged())return;
    if(index<0||index>=static_cast<int>(current.torches.size())||correct<0||correct>1||lit<0||lit>1||
       torch.lit_left_ms<0||torch.lit_left_ms>15000||torch.rearm_ms<0||torch.rearm_ms>2000)return;
    torch.correct=correct==1;torch.lit=lit==1;torch.updated=GetTickCount64();
    std::lock_guard lock(guard);current.torches[static_cast<std::size_t>(index)]=torch;
}
void fire_impl() {
    gsc_args::Arguments args;FireAim fire;int sides=0;
    if(!args.capture(9)||!args.vector(0,fire.points[0])||!args.integer(1,fire.torch[0])||!args.vector(2,fire.points[1])||
       !args.integer(3,fire.torch[1])||!args.vector(4,fire.points[2])||!args.integer(5,fire.torch[2])||!args.integer(6,sides)||
       !args.vector(7,fire.muzzle)||!args.vector(8,fire.forward)||!args.unchanged())return;
    for(const int t:fire.torch)if(t< -1||t>=static_cast<int>(current.torches.size()))return;
    if(sides<0||sides>1||std::abs(nav::dot(fire.forward,fire.forward)-1)>0.01f)return;
    fire.sides=sides==1;fire.updated=GetTickCount64();
    std::lock_guard lock(guard);current.fire=fire;
}
template<void(*Callback)()> void invoke() noexcept {
    if(!ready.load())return;
    try {Callback();}catch(...) {clear();}
}
void PLUTONIUM_CALLBACK smoke_callback() {invoke<smoke_impl>();}
void PLUTONIUM_CALLBACK aim_callback() {invoke<aim_impl>();}
void PLUTONIUM_CALLBACK torch_callback() {invoke<torch_impl>();}
void PLUTONIUM_CALLBACK fire_callback() {invoke<fire_impl>();}
}
bool startup(plutonium::sdk::iinterface* api) {
    ready=false;clear();if(!api)return false;
    // Register before the check so the helper's builtinfunctionexists guards still
    // succeed; mismatched builds simply ignore the calls.
    api->gsc()->register_function("GnosisWindSmoke",smoke_callback);
    api->gsc()->register_function("GnosisWindAim",aim_callback);
    api->gsc()->register_function("GnosisFireTorch",torch_callback);
    api->gsc()->register_function("GnosisFireAim",fire_callback);
    ready=gsc_args::bindings_match();
    return ready;
}
void clear() {std::lock_guard lock(guard);current=View{};}
View snapshot() {std::lock_guard lock(guard);return current;}
}
