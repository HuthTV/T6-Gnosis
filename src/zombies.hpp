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
// The engine's route for an actor (path_t at actor +0x540; layout from its own consumers,
// research/docs/next-node-consumer.md): up to 32 points of 0x1C bytes (position, 2D
// direction, segment length, signed node index or -1 for a synthetic point), the int16
// length at +0x8C0 and the final goal at +0x8D0. Point 0 is the far end (the goal itself,
// or the last stored point of a longer route, which is planned further as he walks) and
// point length-1 the start. Live TranZit check 2026-10-30: 82 node-indexed points on three
// zombies, every one exactly on its NAV node, and a full 32-point route cut at the far end.
inline constexpr std::uint32_t route_points=0x540,route_length=0x8C0,route_goal=0x8D0,route_capacity=32,route_stride=0x1C;
struct Route {std::vector<nav::Vec3> points;nav::Vec3 goal;bool complete=false;};
// Decodes a copy of the route points into walking order (start first). Rejects the whole
// route unless every node-indexed point sits on its NAV node and the start is near the
// actor (it replans as it walks).
inline bool decode_route(const unsigned char* raw,int length,nav::Vec3 goal,const nav::Graph& g,nav::Vec3 actor,Route& out) {
    out=Route{};
    if(length<1||length>static_cast<int>(route_capacity)||!nav::finite(goal))return false;
    for(int i=length-1;i>=0;--i) {
        const unsigned char* p=raw+static_cast<std::size_t>(i)*route_stride;
        const auto point=field<nav::Vec3>(p,0);const auto node=field<std::int32_t>(p,24);
        if(!nav::finite(point)||std::abs(point.x)>1000000||std::abs(point.y)>1000000||std::abs(point.z)>1000000)return false;
        if(node< -1||(node>=0&&static_cast<std::size_t>(node)>=g.nodes.size()))return false;
        if(node>=0) {const auto d=g.nodes[static_cast<std::size_t>(node)].origin-point;if(nav::dot(d,d)>1)return false;}
        out.points.push_back(point);
    }
    const auto start=out.points.front()-actor;
    if(start.x*start.x+start.y*start.y>768.0f*768.0f)return false;
    const auto end=out.points.back()-goal;
    out.goal=goal;out.complete=nav::dot(end,end)<=1;
    return true;
}
// First route point still ahead of the actor: a point is passed once the actor is closer
// (2D) to the following point than the point itself is.
inline std::size_t route_ahead(const std::vector<nav::Vec3>& points,nav::Vec3 actor) {
    auto planar=[](nav::Vec3 a,nav::Vec3 b) {const float x=a.x-b.x,y=a.y-b.y;return x*x+y*y;};
    std::size_t i=0;
    while(i+1<points.size()&&planar(actor,points[i+1])<planar(points[i],points[i+1]))++i;
    return i;
}
bool startup();
void update();
void clear();
std::shared_ptr<const Snapshot> snapshot();
bool still_living(const Marker& marker,nav::Vec3& origin);
// Live origin plus entity bounds (local mins/maxs) of a still-living marker.
bool living_bounds(const Marker& marker,nav::Vec3& origin,nav::Vec3& mins,nav::Vec3& maxs);
// The still-living marker's current route (decode_route), read twice and kept only when
// both reads agree; origin is its live position.
bool route(const Marker& marker,const nav::Graph& g,Route& out,nav::Vec3& origin);
const char* status();
}
