#pragma once
#include "navigation.hpp"
#include <array>
#include <cstring>

namespace zombies {
struct Marker {
    std::uint32_t entity=0,actor=0,sentient=0;
    std::uint16_t number=0,classname=0;
    int special=0;
};
struct Snapshot {std::shared_ptr<const nav::Graph> map;std::vector<Marker> markers;std::uint64_t updated=0;};
template<class T> T field(const unsigned char* data,std::size_t at) {T v{};std::memcpy(&v,data+at,sizeof(v));return v;}
inline bool living_entity(const unsigned char* e,std::uint32_t number,std::uint32_t actor,std::uint32_t sentient) {
    return e[0xFB]==1 && field<std::uint32_t>(e,0)==number && field<std::uint16_t>(e,0xD8)==16 &&
        field<std::int32_t>(e,0x1A8)>0 && field<std::uint32_t>(e,0x154)==0 &&
        actor!=0 && sentient!=0 && field<std::uint32_t>(e,0x158)==actor && field<std::uint32_t>(e,0x15C)==sentient;
}
inline bool zombie_class(const char* name) {
    return std::strncmp(name,"actor_zm_",9)==0||std::strncmp(name,"actor_zombie",12)==0;
}
inline bool actor_address(std::uint32_t root,std::uint32_t actor) {
    return root>=0x10000&&actor>=root&&(actor-root)%0x2720==0&&(actor-root)/0x2720<32;
}
inline nav::Vec3 rotate(nav::Vec3 local,nav::Vec3 degrees) {
    constexpr float rad=0.01745329251994329577f;
    const float sp=std::sin(degrees.x*rad),cp=std::cos(degrees.x*rad),sy=std::sin(degrees.y*rad),cy=std::cos(degrees.y*rad),sr=std::sin(degrees.z*rad),cr=std::cos(degrees.z*rad);
    const nav::Vec3 forward{cp*cy,cp*sy,-sp},left{sr*sp*cy-cr*sy,sr*sp*sy+cr*cy,sr*cp},up{cr*sp*cy+sr*sy,cr*sp*sy-sr*cy,cr*cp};
    return forward*local.x+left*local.y+up*local.z;
}
// Observed standing heads are about 56-59 units above the entity origin.
// A constant approximation avoids cached-pose availability and LOD changes.
// Recompute from the latest verified origin at draw time; retain no history.
inline nav::Vec3 marker_anchor(nav::Vec3 origin) {return origin+nav::Vec3{0,0,60};}
bool startup();
void update();
void clear();
std::shared_ptr<const Snapshot> snapshot();
bool still_living(const Marker& marker,nav::Vec3& origin);
// Live origin plus entity bounds (local mins/maxs) of a still-living marker.
bool living_bounds(const Marker& marker,nav::Vec3& origin,nav::Vec3& mins,nav::Vec3& maxs);
const char* status();
}
