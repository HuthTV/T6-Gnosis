#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>
#include "navigation.hpp"

// Playable area as one merged surface (user requests 2026-10-13/20: overlapping boxes
// were hard to read, seams between volumes drew corner lines, and faces popped in and
// out as volumes entered the draw radius). From convex volumes (the enabled and life
// player volumes), build_surface keeps each face minus all other volumes, and an edge
// only where the merged surface bends. Pure geometry, unit tested.
namespace area {
struct Piece {std::vector<nav::Vec3> points;nav::Vec3 lo,hi;bool life=false;};
struct Edge {nav::Vec3 a,b;bool life=false;};
struct Hull {std::vector<std::pair<nav::Vec3,float>> planes;std::vector<std::vector<nav::Vec3>> faces;nav::Vec3 lo,hi,centre;bool life=false;};
inline constexpr float epsilon=0.25f;
inline nav::Vec3 cross(nav::Vec3 a,nav::Vec3 b) {return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline void grow(nav::Vec3& lo,nav::Vec3& hi,nav::Vec3 p) {
    lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};
}
// A convex hull from its faces (world points, any winding); false when degenerate.
inline bool make_hull(std::vector<std::vector<nav::Vec3>> faces,bool life,Hull& out) {
    out=Hull{};out.life=life;
    nav::Vec3 centre{};unsigned count=0;
    for(const auto& face:faces)for(const auto p:face) {centre=centre+p;++count;}
    if(!count)return false;
    out.centre=centre*(1.0f/static_cast<float>(count));out.lo=out.hi=out.centre;
    for(const auto& face:faces)for(const auto p:face)grow(out.lo,out.hi,p);
    for(const auto& face:faces) {
        if(face.size()<3)continue;
        auto n=cross(face[1]-face[0],face[2]-face[0]);const float length=std::sqrt(nav::dot(n,n));if(length<1e-4f)continue;
        n=n*(1.0f/length);if(nav::dot(n,out.centre-face[0])>0)n=n*-1.0f;
        out.planes.push_back({n,nav::dot(n,face[0])});
    }
    out.faces=std::move(faces);
    return out.planes.size()>=4;
}
// Splits a convex polygon by the plane n.p = d into the parts outside (n.p - d > 0)
// and inside; points on the plane go to both. A part with fewer than 3 points is empty.
inline void split_polygon(const std::vector<nav::Vec3>& polygon,nav::Vec3 n,float d,std::vector<nav::Vec3>& outside,std::vector<nav::Vec3>& inside) {
    outside.clear();inside.clear();
    const auto count=polygon.size();
    for(std::size_t i=0;i<count;++i) {
        const auto a=polygon[i],b=polygon[(i+1)%count];
        const float da=nav::dot(n,a)-d,db=nav::dot(n,b)-d;
        const int sa=da>epsilon?1:da< -epsilon?-1:0,sb=db>epsilon?1:db< -epsilon?-1:0;
        if(sa>=0)outside.push_back(a);
        if(sa<=0)inside.push_back(a);
        if(sa*sb<0) {const auto p=a+(b-a)*(da/(da-db));outside.push_back(p);inside.push_back(p);}
    }
    if(outside.size()<3)outside.clear();
    if(inside.size()<3)inside.clear();
}
inline bool on_plane(const std::vector<nav::Vec3>& polygon,nav::Vec3 n,float d) {
    for(const auto p:polygon)if(std::abs(nav::dot(n,p)-d)>epsilon)return false;
    return true;
}
// The parts of polygon (a face of hull own, outward normal normal) outside hull other.
// A face on one of other's planes is a seam (opposite normals: removed where covered)
// or a coincident outer face (same normal: only the later hull's copy is removed).
inline std::vector<std::vector<nav::Vec3>> subtract_hull(const std::vector<nav::Vec3>& polygon,nav::Vec3 normal,std::size_t own,const Hull& other,std::size_t other_index) {
    std::vector<std::vector<nav::Vec3>> kept;
    std::vector<bool> coplanar(other.planes.size());
    for(std::size_t i=0;i<other.planes.size();++i) {
        coplanar[i]=on_plane(polygon,other.planes[i].first,other.planes[i].second);
        if(coplanar[i]&&nav::dot(other.planes[i].first,normal)>0&&other_index>own)return {polygon};
    }
    auto inside=polygon;std::vector<nav::Vec3> out,in;
    for(std::size_t i=0;i<other.planes.size()&&!inside.empty();++i) {
        if(coplanar[i])continue;
        split_polygon(inside,other.planes[i].first,other.planes[i].second,out,in);
        if(!out.empty())kept.push_back(out);
        inside=in;
    }
    return kept;  // what is left of inside lies within other
}
// Uniform grid over the hulls' bounds, so each face and point is tested only against
// nearby hulls (a whole map holds hundreds of volumes).
struct Grid {
    static constexpr float cell=256;
    std::unordered_map<std::uint64_t,std::vector<std::size_t>> cells;
    std::vector<std::size_t> everywhere;  // hulls too large to bucket
    static std::int64_t at(float v) {return static_cast<std::int64_t>(std::floor(v/cell));}
    static std::uint64_t key(std::int64_t x,std::int64_t y,std::int64_t z) {
        return (static_cast<std::uint64_t>(x+(1<<20))<<42)|(static_cast<std::uint64_t>(y+(1<<20))<<21)|static_cast<std::uint64_t>(z+(1<<20));
    }
    explicit Grid(const std::vector<Hull>& hulls) {
        for(std::size_t i=0;i<hulls.size();++i) {
            const auto& h=hulls[i];
            const auto x0=at(h.lo.x-1),x1=at(h.hi.x+1),y0=at(h.lo.y-1),y1=at(h.hi.y+1),z0=at(h.lo.z-1),z1=at(h.hi.z+1);
            if((x1-x0+1)*(y1-y0+1)*(z1-z0+1)>512) {everywhere.push_back(i);continue;}
            for(auto x=x0;x<=x1;++x)for(auto y=y0;y<=y1;++y)for(auto z=z0;z<=z1;++z)cells[key(x,y,z)].push_back(i);
        }
    }
    // Indices of hulls that may touch the box lo..hi, ascending, without repeats.
    std::vector<std::size_t> nearby(nav::Vec3 lo,nav::Vec3 hi) const {
        std::vector<std::size_t> out(everywhere);
        const auto x0=at(lo.x),x1=at(hi.x),y0=at(lo.y),y1=at(hi.y),z0=at(lo.z),z1=at(hi.z);
        for(auto x=x0;x<=x1;++x)for(auto y=y0;y<=y1;++y)for(auto z=z0;z<=z1;++z) {
            const auto found=cells.find(key(x,y,z));
            if(found!=cells.end())out.insert(out.end(),found->second.begin(),found->second.end());
        }
        std::sort(out.begin(),out.end());out.erase(std::unique(out.begin(),out.end()),out.end());
        return out;
    }
};
inline bool inside_any(const std::vector<Hull>& hulls,const Grid& grid,nav::Vec3 p) {
    for(const auto index:grid.nearby(p,p)) {
        const auto& hull=hulls[index];
        if(p.x<hull.lo.x||p.y<hull.lo.y||p.z<hull.lo.z||p.x>hull.hi.x||p.y>hull.hi.y||p.z>hull.hi.z)continue;
        bool inside=true;
        for(const auto& plane:hull.planes)if(nav::dot(plane.first,p)-plane.second>-0.5f) {inside=false;break;}
        if(inside)return true;
    }
    return false;
}
inline void build_surface(const std::vector<Hull>& hulls,std::vector<Piece>& pieces_out,std::vector<Edge>& edges_out) {
    pieces_out.clear();edges_out.clear();
    auto overlaps=[](const Hull& a,nav::Vec3 lo,nav::Vec3 hi) {
        constexpr float m=1;
        return a.hi.x+m>=lo.x&&a.lo.x-m<=hi.x&&a.hi.y+m>=lo.y&&a.lo.y-m<=hi.y&&a.hi.z+m>=lo.z&&a.lo.z-m<=hi.z;
    };
    std::set<std::array<long,6>> drawn;
    const Grid grid(hulls);
    for(std::size_t h=0;h<hulls.size();++h)for(const auto& face:hulls[h].faces) {
        if(face.size()<3)continue;
        auto normal=cross(face[1]-face[0],face[2]-face[0]);const float length=std::sqrt(nav::dot(normal,normal));if(length<1e-4f)continue;
        normal=normal*(1.0f/length);
        nav::Vec3 centre{};for(const auto p:face)centre=centre+p;centre=centre*(1.0f/static_cast<float>(face.size()));
        if(nav::dot(normal,hulls[h].centre-centre)>0)normal=normal*-1.0f;
        std::vector<std::vector<nav::Vec3>> pieces{face};
        nav::Vec3 face_lo=face[0],face_hi=face[0];for(const auto p:face)grow(face_lo,face_hi,p);
        for(const auto o:grid.nearby(face_lo-nav::Vec3{1,1,1},face_hi+nav::Vec3{1,1,1})) {
            if(pieces.empty())break;
            if(o==h)continue;
            std::vector<std::vector<nav::Vec3>> next;
            for(const auto& piece:pieces) {
                nav::Vec3 lo=piece[0],hi=piece[0];for(const auto p:piece)grow(lo,hi,p);
                if(!overlaps(hulls[o],lo,hi)) {next.push_back(piece);continue;}
                for(auto& part:subtract_hull(piece,normal,h,hulls[o],o))next.push_back(std::move(part));
            }
            pieces=std::move(next);
        }
        for(auto& piece:pieces) {
            Piece out;out.life=hulls[h].life;out.lo=out.hi=piece[0];for(const auto p:piece)grow(out.lo,out.hi,p);
            // An edge is drawn only where the surface bends: just past it, the merged
            // surface does not continue flat (inside below, outside above).
            nav::Vec3 piece_centre{};for(const auto p:piece)piece_centre=piece_centre+p;piece_centre=piece_centre*(1.0f/static_cast<float>(piece.size()));
            for(std::size_t i=0;i<piece.size();++i) {
                const auto a=piece[i],b=piece[(i+1)%piece.size()];const auto e=b-a;
                if(nav::dot(e,e)<1)continue;
                const auto m=(a+b)*0.5f;auto across=cross(e,normal);
                const float across_length=std::sqrt(nav::dot(across,across));if(across_length<1e-4f)continue;
                across=across*(1.0f/across_length);if(nav::dot(across,piece_centre-m)>0)across=across*-1.0f;
                const auto beyond=m+across*2.0f;
                if(inside_any(hulls,grid,beyond-normal*2.0f)&&!inside_any(hulls,grid,beyond+normal*2.0f))continue;
                auto key=[](nav::Vec3 p) {return std::array<long,3>{std::lround(p.x),std::lround(p.y),std::lround(p.z)};};
                auto ka=key(a),kb=key(b);if(kb<ka)std::swap(ka,kb);
                if(!drawn.insert({ka[0],ka[1],ka[2],kb[0],kb[1],kb[2]}).second)continue;
                edges_out.push_back({a,b,out.life});
            }
            out.points=std::move(piece);
            pieces_out.push_back(std::move(out));
        }
    }
}
}
