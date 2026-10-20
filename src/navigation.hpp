#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nav {
struct Vec3 { float x=0, y=0, z=0; };
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x*s,a.y*s,a.z*s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline bool finite(Vec3 a) { return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z); }
inline bool within_draw_range(Vec3 point,Vec3 center,float horizontal,float vertical,bool all_heights=false) {
    const auto d=point-center;
    return d.x*d.x+d.y*d.y<=horizontal*horizontal&&(all_heights||std::abs(d.z)<=vertical);
}
struct Node { Vec3 origin; std::uint16_t type=0; std::vector<std::uint16_t> links; };
inline bool traversal_crossing(std::uint16_t from,std::uint16_t to) {return from==17&&to==18;}
struct Graph {
    std::uint32_t data=0, base=0, count=0;
    std::string map;
    std::vector<Node> nodes;
    std::uint32_t directed_links=0;
};
struct Camera {
    int x=0,y=0,width=0,height=0;
    float tan_x=0,tan_y=0;
    Vec3 origin,forward,left,up;
};
struct Point { float x=0,y=0; };
inline bool valid_camera(const Camera& c) {
    if(c.width<160||c.height<120||c.width>16384||c.height>16384||c.x<0||c.y<0) return false;
    if(!std::isfinite(c.tan_x)||!std::isfinite(c.tan_y)||c.tan_x<0.05f||c.tan_y<0.05f||c.tan_x>10||c.tan_y>10) return false;
    if(!finite(c.origin)||!finite(c.forward)||!finite(c.left)||!finite(c.up)) return false;
    return std::abs(dot(c.forward,c.forward)-1)<0.03f && std::abs(dot(c.left,c.left)-1)<0.03f &&
        std::abs(dot(c.up,c.up)-1)<0.03f && std::abs(dot(c.forward,c.left))<0.03f &&
        std::abs(dot(c.forward,c.up))<0.03f && std::abs(dot(c.left,c.up))<0.03f;
}
inline Vec3 camera_space(const Camera& c,Vec3 p) {
    auto d=p-c.origin;return {dot(d,c.left),dot(d,c.up),dot(d,c.forward)};
}
inline Point project(const Camera& c,Vec3 p) {
    return {c.x+c.width*0.5f*(1-p.x/(p.z*c.tan_x)),c.y+c.height*0.5f*(1-p.y/(p.z*c.tan_y))};
}
// Clip in camera space before dividing by depth. A crossing edge remains visible
// even when one endpoint is behind the eye or both endpoints are off screen.
inline bool clip_plane(Vec3& a,Vec3& b,Vec3 normal,float offset) {
    const float da=dot(normal,a)+offset, db=dot(normal,b)+offset;
    if(da<0 && db<0)return false;
    if((da<0)!=(db<0)) {
        const Vec3 p=a+(b-a)*(da/(da-db));
        if(da<0)a=p;else b=p;
    }
    return true;
}
inline bool clip_segment(const Camera& c,Vec3& a,Vec3& b) {
    return clip_plane(a,b,{0,0,1},-1.0f) &&
        clip_plane(a,b,{1,0,c.tan_x},0) && clip_plane(a,b,{-1,0,c.tan_x},0) &&
        clip_plane(a,b,{0,1,c.tan_y},0) && clip_plane(a,b,{0,-1,c.tan_y},0);
}
inline std::vector<Vec3> clip_polygon(const Camera& c,std::vector<Vec3> points) {
    const Vec3 normals[]={{0,0,1},{1,0,c.tan_x},{-1,0,c.tan_x},{0,1,c.tan_y},{0,-1,c.tan_y}};
    for(unsigned plane=0;plane<5&&!points.empty();++plane) {
        std::vector<Vec3> clipped;const float offset=plane==0?-1.0f:0.0f;
        auto a=points.back();float da=dot(normals[plane],a)+offset;
        for(const auto b:points) {
            const float db=dot(normals[plane],b)+offset;
            if((da<0)!=(db<0))clipped.push_back(a+(b-a)*(da/(da-db)));
            if(db>=0)clipped.push_back(b);
            a=b;da=db;
        }
        points=std::move(clipped);
    }
    return points;
}
inline bool point_visible(const Camera& c,Vec3 p) {
    return p.z>=1 && std::abs(p.x)<=p.z*c.tan_x && std::abs(p.y)<=p.z*c.tan_y;
}
bool bindings_match();
void refresh_graph();
void clear_graph(int unused=0);
std::shared_ptr<const Graph> graph();
bool read_camera(Camera& out,const Graph& g);
const char* status();
}
