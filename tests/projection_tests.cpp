#include "navigation.hpp"
#include "triggers.hpp"
#include <cstdio>
#include <limits>

namespace {int failures=0;
void check(bool ok,const char* why) {if(!ok){std::printf("FAIL: %s\n",why);++failures;}}
bool near(float a,float b) {return std::abs(a-b)<0.01f;}
}
int main() {
    triggers::Mesh body_test;triggers::box_mesh({-1,-1,-1},{1,1,1},body_test);const auto no_transform=[](nav::Vec3 p){return p;};
    check(triggers::bounds_overlap(body_test,{2,0,0},{1,0.5f,35},no_transform),"rectangular player body touching trigger intersects");
    check(!triggers::bounds_overlap(body_test,{2.1f,0,0},{1,0.5f,35},no_transform),"player body separation rejected");
    check(!triggers::cylinder_bounds_overlap({0,0,0},1,2,{2,-0.5f,0},{3,0.5f,70}),"player body outside circular trigger rejected");
    check(triggers::cylinder_bounds_overlap({0,0,0},1,2,{1,-0.5f,0},{2,0.5f,70}),"player body touching circular trigger overlaps");

    check(triggers::brush_list_address(0x89000000,1),"independently allocated brush list address accepted");
    check(!triggers::brush_list_address(0x89000001,1)&&!triggers::brush_list_address(0x89000000,65),"misaligned and oversized brush lists rejected");
    check(!triggers::brush_list_address(0xFFFFFFFE,2)&&!triggers::brush_list_address(0,1)&&!triggers::brush_list_address(0x89000000,0),"overflow null and empty brush list rejected");

    check(triggers::region({{"script_noteworthy","player_volume"},{"zone_enabled","1"}})==triggers::Region::enabled_player,"enabled zone player volume is green");
    check(!triggers::playable_region(triggers::region({{"script_noteworthy","player_volume"},{"zone_enabled","0"}})),"disabled zone never included as safe region");
    check(!triggers::playable_region(triggers::region({{"script_noteworthy","player_volume"}})),"missing zone state never assumed enabled");
    check(triggers::region({{"stub.script_noteworthy","life_brush"}})==triggers::Region::none,"stub description does not masquerade as queried physical life brush");
    check(triggers::region({{"script_noteworthy","life_brush"}})==triggers::Region::life&&triggers::region({{"script_noteworthy","kill_brush"}})==triggers::Region::kill,"life and kill volume classification");
    check(triggers::base_monitor_safe(true,true,false),"life brush overrides kill/outside condition");
    check(!triggers::base_monitor_safe(false,true,true),"kill brush overrides enabled player region");
    check(triggers::base_monitor_safe(false,false,true)&&!triggers::base_monitor_safe(false,false,false),"enabled region required without life exemption");
    check(triggers::label("info_volume",{{"script_noteworthy","player_volume"},{"zone_enabled","1"},{"monitor_override","1"}})=="info_volume\nscript_noteworthy=player_volume","internal monitor state does not expand requested labels");

    triggers::Mesh overlap;triggers::box_mesh({-3,-3,-3},{3,3,3},overlap);
    const auto identity=[](nav::Vec3 p){return p;};
    check(triggers::range_overlap(overlap,{0,0,0},1,1,identity),"large trigger enclosing range draws with every vertex outside");
    check(triggers::range_overlap(overlap,{4,0,0},1,1,identity),"touching trigger boundary draws");
    check(!triggers::range_overlap(overlap,{4.1f,0,0},1,1,identity),"separated trigger outside range hidden");
    triggers::box_mesh({-10,-0.1f,-1},{10,0.1f,1},overlap);
    const auto slanted=[](nav::Vec3 p){const float s=std::sqrt(0.5f);return nav::Vec3{(p.x-p.y)*s,(p.x+p.y)*s, p.z};};
    check(!triggers::range_overlap(overlap,{5,-5,0},1,1,slanted),"rotated hull enclosing AABB overlap alone does not draw");
    check(triggers::range_overlap(overlap,{5,5,0},1,1,slanted),"rotated trigger edge overlaps range even with origin outside");
    check(triggers::cylinder_range_overlap({3,0,-1},2,2,{0,0,0},1,1),"cylinder boundary touches square range");
    check(!triggers::cylinder_range_overlap({3,3,-1},2,2,{0,0,0},1,1),"cylinder bounding box corner outside actual circle rejected");
    check(!triggers::cylinder_range_overlap({0,0,3},2,2,{0,0,0},1,1),"vertical cylinder separation rejected");
    check(triggers::label("trigger_box",{{"target","ignored"},{"stub.targetname","fallback"},{"targetname","direct"},{"script_noteworthy","tank"}})=="trigger_box\nscript_noteworthy=tank\ntargetname=direct","labels contain only type and requested fields with direct precedence");
    check(triggers::label("info_volume",{})=="info_volume","missing metadata adds no purpose placeholder");

    std::array<unsigned char,0x31C> old_pose{},new_pose{};new_pose[0x134]=1;new_pose[0x140]=2;new_pose[0x11C]=3;
    check(triggers::stable_shape(old_pose.data(),new_pose.data()),"moving/rotating trigger and broad bounds do not disappear");
    new_pose[0x310]=1;check(!triggers::stable_shape(old_pose.data(),new_pose.data()),"reused entity lifetime rejected despite identical shape");
    new_pose[0x310]=0;new_pose[0x100]=1;check(!triggers::stable_shape(old_pose.data(),new_pose.data()),"changed local shape requires recapture");

    nav::Camera clipping{};clipping.tan_x=clipping.tan_y=1;
    const auto partial=nav::clip_polygon(clipping,{{-4,-1,2},{4,-1,2},{4,1,2},{-4,1,2}});
    check(partial.size()==4,"partially offscreen face keeps clipped fill");
    bool clipped_inside=true;for(const auto p:partial)clipped_inside=clipped_inside&&p.z>=1&&std::abs(p.x)<=p.z+0.001f&&std::abs(p.y)<=p.z+0.001f;
    check(clipped_inside,"clipped face vertices remain inside camera planes");
    check(nav::clip_polygon(clipping,{{-1,-1,-3},{1,-1,-3},{0,1,-3}}).empty(),"face behind camera rejected");
    const auto crossing=nav::clip_polygon(clipping,{{-0.1f,-0.1f,0},{0.1f,-0.1f,2},{0,0.1f,2}});
    check(crossing.size()>=3,"face crossing near plane remains visible");
    check(triggers::candidate("info_volume"),"info volume reader supports separate display toggle");

    const triggers::CatalogEntry ref={"zm_tomb","targetname","test_trigger","source:10 consumer","",{},{}};
    const std::vector<triggers::ScriptField> metadata={{"stub.targetname","test_trigger"}};
    check(triggers::metadata_match(ref,metadata,"maps/mp/zm_tomb.d3dbsp"),"live stub metadata matches map script reference");
    check(!triggers::metadata_match(ref,metadata,"maps/mp/zm_buried.d3dbsp"),"same trigger name on different map rejected");
    check(!triggers::metadata_match(ref,{{"target","test_trigger"}},"maps/mp/zm_tomb.d3dbsp"),"different metadata field is not conflated");
    const triggers::CatalogEntry geo={"zm_tomb","","","source:20 spawn","trigger_box",{1,2,3},{64,192,128}};
    check(triggers::geometry_candidate(geo,"maps/mp/zm_tomb.d3dbsp","trigger_box",{1,2,3},{64,192,128}),"complete literal spawn matches geometry candidate");
    check(!triggers::geometry_candidate(geo,"maps/mp/zm_tomb.d3dbsp","trigger_radius",{1,2,3},{64,192,128}),"geometry candidate requires actual classname");
    check(!triggers::geometry_candidate(geo,"maps/mp/zm_tomb.d3dbsp","trigger_box",{1,2,4},{64,192,128}),"nearby spawn is not identified by proximity");
    check(triggers::metadata_key("stub.script_noteworthy")&&!triggers::metadata_key("health"),"bridge only accepts descriptive fields");

    triggers::Mesh shape;
    check(triggers::box_mesh({-32,-96,-64},{32,96,64},shape)&&shape.faces.size()==6,"measured target box has six actual faces");
    bool exact=true;for(const auto& face:shape.faces)for(const auto v:face)exact=exact&&near(std::abs(v.x),32)&&near(std::abs(v.y),96)&&near(std::abs(v.z),64);
    check(exact,"box vertices match native extents rather than broad enclosing cube");
    auto cut=triggers::axial_planes({-1,-1,-1},{1,1,1});const float inv=1/std::sqrt(3.0f);cut.push_back({{inv,inv,inv},inv});
    check(triggers::convex_mesh(cut,shape)&&shape.faces.size()==7,"nonaxial brush plane clips actual hull");
    bool inside=true;for(const auto& face:shape.faces)for(const auto v:face)inside=inside&&(v.x+v.y+v.z<=1.03f);
    check(inside,"cut brush does not render the enclosing-box removed corner");
    cut.push_back({{1,0,0},-2});check(!triggers::convex_mesh(cut,shape),"empty inconsistent hull rejected");
    check(!triggers::box_mesh({1,0,0},{0,1,1},shape),"inverted bounds rejected");
    check(triggers::radial_bounds({-72,-72,0},{72,72,96})&&!triggers::radial_bounds({-72,-60,0},{72,72,96}),"cylinder contract requires symmetric native radial extents");
    check(triggers::cylinder_mesh(72,96,shape)&&shape.faces.size()==34,"native cylinder radius/height recovered");
    bool radial=true;for(const auto& face:shape.faces)for(const auto v:face)radial=radial&&near(v.x*v.x+v.y*v.y,72*72)&&(near(v.z,0)||near(v.z,96));
    check(radial,"cylinder ring vertices lie on radial boundary with bottom at origin");
    check(!triggers::cylinder_mesh(-1,96,shape)&&!triggers::cylinder_mesh(72,0,shape),"malformed cylinder rejected");
    check(triggers::candidate("trigger_box")&&triggers::candidate("trigger_radius_use"),"trigger classes included");
    check(!triggers::candidate(nullptr)&&!triggers::candidate("script_model")&&!triggers::candidate("actor_zombie"),"nonregions excluded");
    auto trigger_map=std::make_shared<nav::Graph>();trigger_map->map="maps/mp/zm_tomb.d3dbsp";trigger_map->base=1;trigger_map->data=2;
    triggers::Snapshot trigger_sample{};trigger_sample.map=trigger_map;trigger_sample.updated=500;
    check(triggers::fresh(trigger_sample,*trigger_map,1500),"fresh trigger snapshot accepted at age limit");
    check(!triggers::fresh(trigger_sample,*trigger_map,1501)&&!triggers::fresh(trigger_sample,*trigger_map,499),"stale/future trigger snapshots hidden");
    auto other_map=*trigger_map;other_map.map="maps/mp/zm_buried.d3dbsp";
    check(!triggers::fresh(trigger_sample,other_map,600),"Origins markers rejected on another map");
    other_map=*trigger_map;other_map.data=3;
    check(!triggers::fresh(trigger_sample,other_map,600),"new map allocation rejects old markers");
    check(nav::within_draw_range({100,0,50},{0,0,0},100,50),"horizontal radius and vertical boundary are independent");
    check(!nav::within_draw_range({80,80,0},{0,0,0},100,50),"horizontal range is circular rather than square");
    check(!nav::within_draw_range({0,0,51},{0,0,0},100,50),"vertical limit rejects points above band");
    check(nav::within_draw_range({100,0,500},{0,0,0},100,50,true),"all heights bypasses only vertical limit");
    check(nav::traversal_crossing(17,18),"negotiation crossing arrow enabled");
    check(!nav::traversal_crossing(18,17)&&!nav::traversal_crossing(17,8)&&!nav::traversal_crossing(1,1),"arrows excluded for reverse, pillar and ordinary links");
    nav::Camera c{};c.width=1920;c.height=1080;c.tan_x=1;c.tan_y=0.5625f;
    c.forward={1,0,0};c.left={0,1,0};c.up={0,0,1};
    check(nav::valid_camera(c),"orthonormal camera accepted");
    auto center=nav::project(c,nav::camera_space(c,{100,0,0}));
    check(near(center.x,960)&&near(center.y,540),"forward axis lands on crosshair");
    const auto left=nav::project(c,nav::camera_space(c,{100,100,0}));
    check(near(left.x,0),"left axis lands on left edge");
    auto a=nav::camera_space(c,{-10,0,0}),b=nav::camera_space(c,{100,0,0});
    check(nav::clip_segment(c,a,b)&&near(a.z,1),"camera crossing clipped to positive near plane");
    a={0,0,-10};b={0,0,-1};check(!nav::clip_segment(c,a,b),"behind-camera segment rejected");
    a={-200,0,100};b={200,0,100};
    check(nav::clip_segment(c,a,b)&&near(a.x,-100)&&near(b.x,100),"two offscreen endpoints crossing view retained");
    a={200,0,100};b={300,0,100};check(!nav::clip_segment(c,a,b),"outside frustum segment rejected");
    a={0,-100,100};b={0,100,100};check(nav::clip_segment(c,a,b)&&near(a.y,-56.25f)&&near(b.y,56.25f),"vertical clipping");
    check(!nav::point_visible(c,{0,0,0.5f}),"near point rejected");
    check(!nav::point_visible(c,{101,0,100}),"offscreen dot rejected");
    auto invalid=c;invalid.forward={0,0,0};check(!nav::valid_camera(invalid),"zero basis rejected");
    invalid=c;invalid.left=invalid.forward;check(!nav::valid_camera(invalid),"collinear basis rejected");
    invalid=c;invalid.tan_x=0;check(!nav::valid_camera(invalid),"zero FOV rejected");
    invalid=c;invalid.origin.z=std::numeric_limits<float>::quiet_NaN();check(!nav::valid_camera(invalid),"NaN origin rejected");
    invalid=c;invalid.width=0;check(!nav::valid_camera(invalid),"loading viewport rejected");
    c.origin={17,-42,88};c.forward={0,1,0};c.left={-1,0,0};
    center=nav::project(c,nav::camera_space(c,c.origin+c.forward*100));
    check(near(center.x,960)&&near(center.y,540),"translated and rotated camera aligned");
    std::printf("Projection/camera checks: %s (%d failures)\n",failures?"FAILED":"PASS",failures);
    return failures?1:0;
}
