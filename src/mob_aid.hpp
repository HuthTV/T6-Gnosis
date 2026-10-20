#pragma once
#include <array>
#include <cstdint>
#include <plutonium_sdk.hpp>
#include "navigation.hpp"

// Mob of the Dead blue-skull aid. The GSC helper (gnosis_mob_aid) publishes:
//   GnosisMobState( afterlife, has_tomahawk, player_origin )                  20 Hz
//   GnosisMobSkull( index, origin, absmins, absmaxs, shown_ms_left )          per skull, 20 Hz
//   GnosisTomahawkAim( start, launch_velocity, stop_time, check, skull, impact ) 20 Hz, tomahawk owned
//   GnosisTomahawkFlight( throw, point, is_check, skull )                     each frame of a throw
//   GnosisMobBox( index, origin, absmins, absmaxs, planar )                   per shock box, 1 Hz
// Vanilla rules the drawing shows: a skull is caught when the tomahawk's single check
// point is within catch_radius of the skull origin; a shock box needs the player
// within interact_range (2D for afterlife-door boxes).
namespace mob {
inline constexpr float catch_radius=100,interact_range=256,gravity=800;
struct State {bool afterlife=false,tomahawk=false;nav::Vec3 player;std::uint64_t updated=0;};
struct Skull {nav::Vec3 origin,mins,maxs;float shown_ms=-1;std::uint64_t updated=0;};
struct Aim {nav::Vec3 start,launch,check;float stop=0;int skull=-1;bool impact=false;std::uint64_t updated=0;};
struct Flight {
    int id=0;unsigned count=0;std::array<nav::Vec3,32> points{};
    bool done=false;nav::Vec3 check;int skull=-1;std::uint64_t updated=0;
};
struct Box {nav::Vec3 origin,mins,maxs;bool planar=false;std::uint64_t updated=0;};
struct View {State state;std::array<Skull,8> skulls{};Aim aim;Flight flight;std::array<Box,32> boxes{};};
inline constexpr std::uint64_t live_ms=250,skull_ms=500,box_ms=2500,flight_ms=4000;
inline bool fresh(std::uint64_t updated,std::uint64_t now,std::uint64_t lifetime) {return updated&&now>=updated&&now-updated<=lifetime;}
// Arc position t seconds after the launch.
inline nav::Vec3 arc(const Aim& aim,float t) {return aim.start+aim.launch*t+nav::Vec3{0,0,-0.5f*gravity*t*t};}
bool startup(plutonium::sdk::iinterface* api);
void clear();
View snapshot();
}
