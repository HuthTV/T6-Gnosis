#include "zombies.hpp"
#include <cstdio>
namespace {int failures=0;
void check(bool good,const char* why){if(!good){++failures;std::printf("FAIL: %s\n",why);}}
template<class T>void set(std::array<unsigned char,0x31C>& e,std::size_t at,T value){std::memcpy(e.data()+at,&value,sizeof(value));}
}
int main() {
    std::array<unsigned char,0x31C> live{};set(live,0,22u);set(live,0xD8,std::uint16_t{16});live[0xFB]=1;
    set(live,0x1A8,150);set(live,0x158,0x30000u);set(live,0x15C,0x50000u);
    check(zombies::living_entity(live.data(),22,0x30000,0x50000),"living actor accepted");
    auto e=live;set(e,0x1A8,0);check(!zombies::living_entity(e.data(),22,0x30000,0x50000),"zero health excluded");
    e=live;set(e,0x1A8,-5);check(!zombies::living_entity(e.data(),22,0x30000,0x50000),"negative health excluded");
    e=live;e[0xFB]=0;check(!zombies::living_entity(e.data(),22,0x30000,0x50000),"despawn excluded");
    e=live;set(e,0xD8,std::uint16_t{17});check(!zombies::living_entity(e.data(),22,0x30000,0x50000),"spawn template excluded");
    e=live;set(e,0xD8,std::uint16_t{18});check(!zombies::living_entity(e.data(),22,0x30000,0x50000),"corpse excluded");
    e=live;set(e,0x154,0x90000u);check(!zombies::living_entity(e.data(),22,0x30000,0x50000),"client entity excluded");
    check(!zombies::living_entity(live.data(),23,0x30000,0x50000),"entity-slot mismatch rejected");
    check(!zombies::living_entity(live.data(),22,0x30004,0x50000),"actor reuse/mismatch rejected");
    check(!zombies::living_entity(live.data(),22,0x30000,0x50004),"sentient mismatch rejected");
    check(zombies::zombie_class("actor_zm_nuked_basic_01"),"observed zombie class accepted");
    check(!zombies::zombie_class("player")&&!zombies::zombie_class("script_model")&&!zombies::zombie_class("actor_dog"),"unrelated classes excluded");
    check(zombies::actor_address(0x40000,0x40000+31*0x2720),"last actor slot accepted");
    check(!zombies::actor_address(0x40000,0x40000+32*0x2720),"actor count bound enforced");
    check(!zombies::actor_address(0x40000,0x40001),"misaligned actor rejected");
    const auto p=zombies::rotate({10,0,0},{0,90,0});check(std::abs(p.x)<0.001f&&std::abs(p.y-10)<0.001f,"head translation rotated with model yaw");
    const auto near=zombies::marker_anchor({10,20,30}),far=zombies::marker_anchor({10000,-9000,30});
    check(near.x==10&&near.y==20&&near.z==90,"anchor stays directly above standing entity");
    check(far.z==near.z&&far.x==10000&&far.y==-9000,"distance does not change standing-height anchor");
    const auto moved=zombies::marker_anchor({14,27,35});
    check(moved.x-near.x==4&&moved.y-near.y==7&&moved.z-near.z==5,"anchor tracks current motion without historical lag");
    const auto below=zombies::marker_anchor({0,0,-400});
    check(below.z==-340,"negative map elevation retains constant relative height");
    // Route: point 0 is the far end, point length-1 the start (live TranZit layout).
    nav::Graph g;g.nodes.resize(3);g.nodes[0].origin={0,0,0};g.nodes[1].origin={256,0,0};g.nodes[2].origin={256,256,0};
    std::array<unsigned char,zombies::route_capacity*zombies::route_stride> raw{};
    auto point=[&](int i,nav::Vec3 at,std::int32_t node) {std::memcpy(raw.data()+i*zombies::route_stride,&at,12);std::memcpy(raw.data()+i*zombies::route_stride+24,&node,4);};
    point(0,{300,400,0},-1);point(1,{256,256,0},2);point(2,{256,0,0},1);point(3,{0,0,0},0);
    zombies::Route r;
    check(zombies::decode_route(raw.data(),4,{300,400,0},g,{10,0,0},r)&&r.points.size()==4,"route decoded");
    check(r.points.front().x==0&&r.points.back().y==400&&r.complete,"walking order from the start to the goal");
    check(zombies::decode_route(raw.data(),4,{900,900,0},g,{10,0,0},r)&&!r.complete,"route cut short of a farther goal");
    check(!zombies::decode_route(raw.data(),0,{300,400,0},g,{10,0,0},r)&&!zombies::decode_route(raw.data(),33,{300,400,0},g,{10,0,0},r),"route length bounds");
    check(!zombies::decode_route(raw.data(),4,{300,400,0},g,{2000,0,0},r),"start far from the actor rejected");
    point(1,{250,256,0},2);check(!zombies::decode_route(raw.data(),4,{300,400,0},g,{10,0,0},r),"point off its node rejected");
    point(1,{256,256,0},3);check(!zombies::decode_route(raw.data(),4,{300,400,0},g,{10,0,0},r),"node index outside the graph rejected");
    point(1,{256,256,0},-2);check(!zombies::decode_route(raw.data(),4,{300,400,0},g,{10,0,0},r),"negative index other than -1 rejected");
    point(1,{256,256,0},2);
    check(zombies::decode_route(raw.data(),4,{300,400,0},g,{10,0,0},r),"restored route accepted");
    check(zombies::route_ahead(r.points,{10,0,0})==1,"start already passed on the first segment");
    check(zombies::route_ahead(r.points,{-40,0,0})==0,"start still ahead behind it");
    check(zombies::route_ahead(r.points,{256,200,0})==2,"two points passed further along");
    std::printf("Zombie identification checks: %s (%d failures)\n",failures?"FAIL":"PASS",failures);return failures?1:0;
}
