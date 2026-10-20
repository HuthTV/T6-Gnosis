#pragma once
#include <array>
#include <cstdint>
#include <plutonium_sdk.hpp>
#include "navigation.hpp"

// Origins wind puzzle 2 lineup aid. The GSC helper does the traces and the verdict
// (the native side has no engine trace) and publishes them through two builtins:
//   GnosisWindSmoke( index, absmins, absmaxs, dir_to_dest, solved, cooling )  per smoke, 20 Hz
//   GnosisWindAim( verdict, hit, smoke, point, forward, direction, threshold,
//                  cooldown_ms, world_dist, brush_dist )                       20 Hz, wind staff held
// The overlay only draws samples that are fresh; nothing is extrapolated.
namespace staff {
enum class Verdict {miss=0,fail=1,pass=2,edge=3};
enum class Hit {none=0,direct=1,splash=2};
struct Smoke {
    nav::Vec3 mins,maxs,to_dest;
    bool solved=false,cooling=false;
    std::uint64_t updated=0;
};
struct Aim {
    Verdict verdict=Verdict::miss;Hit hit=Hit::none;int smoke=-1;
    nav::Vec3 point;
    // Inputs of the verdict, for the live readout. direction is vanilla's unnormalized
    // direction_vec (zero when nothing is hit); threshold is cos( script_int ).
    nav::Vec3 forward,direction;
    float threshold=0,cooldown_ms=0,world_dist=0,brush_dist=0;
    std::uint64_t updated=0;
};
// Yaw/pitch in degrees, game convention: yaw from +x toward +y, pitch positive downward.
inline void angles(nav::Vec3 v,float& yaw,float& pitch) {
    yaw=std::atan2(v.y,v.x)*57.2957795f;if(yaw<0)yaw+=360;
    pitch=-std::atan2(v.z,std::sqrt(v.x*v.x+v.y*v.y))*57.2957795f;
}
// Angle in degrees between two vectors, or -1 when either is zero.
inline float angle_between(nav::Vec3 a,nav::Vec3 b) {
    const float la=std::sqrt(nav::dot(a,a)),lb=std::sqrt(nav::dot(b,b));
    if(la<1e-6f||lb<1e-6f)return -1;
    return std::acos(std::clamp(nav::dot(a,b)/(la*lb),-1.0f,1.0f))*57.2957795f;
}
// Origins fire puzzle 2:
//   GnosisFireTorch( index, origin, correct, lit, lit_left_ms, rearm_ms )            per torch, 20 Hz
//   GnosisFireAim( main, main_torch, left, left_torch, right, right_torch,
//                  sides_fire, muzzle, forward )                                     20 Hz, fire staff held
inline constexpr float torch_radius=64;
struct Torch {
    nav::Vec3 origin;
    bool correct=false,lit=false;
    float lit_left_ms=0,rearm_ms=0;
    std::uint64_t updated=0;
};
struct FireAim {
    // Main, then the left (-15) and right (+15) magicbullets; torch -1 when none.
    std::array<nav::Vec3,3> points{};std::array<int,3> torch{-1,-1,-1};
    bool sides=false;
    nav::Vec3 muzzle,forward;
    std::uint64_t updated=0;
};
struct View {std::array<Smoke,3> smokes{};Aim aim{};std::array<Torch,9> torches{};FireAim fire{};};
inline constexpr std::uint64_t smoke_lifetime_ms=500,aim_lifetime_ms=250;
inline bool fresh(std::uint64_t updated,std::uint64_t now,std::uint64_t lifetime) {return updated&&now>=updated&&now-updated<=lifetime;}
bool startup(plutonium::sdk::iinterface* api);
void clear();
View snapshot();
}
