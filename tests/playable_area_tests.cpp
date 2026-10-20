// Merged playable-area surface: neighbouring and overlapping volumes form one surface,
// with no seam faces and no edges where the surface continues flat.
#include <chrono>
#include <cstdio>
#include "playable_area.hpp"

namespace {
int failures=0,checks=0;
void check(bool ok,const char* what) {++checks;if(!ok){++failures;std::printf("FAIL: %s\n",what);}}
// Axis-aligned box as six quad faces.
area::Hull box(nav::Vec3 lo,nav::Vec3 hi) {
    const nav::Vec3 c[8]={{lo.x,lo.y,lo.z},{hi.x,lo.y,lo.z},{hi.x,hi.y,lo.z},{lo.x,hi.y,lo.z},{lo.x,lo.y,hi.z},{hi.x,lo.y,hi.z},{hi.x,hi.y,hi.z},{lo.x,hi.y,hi.z}};
    const int q[6][4]={{0,1,2,3},{4,5,6,7},{0,1,5,4},{1,2,6,5},{2,3,7,6},{3,0,4,7}};
    std::vector<std::vector<nav::Vec3>> faces;
    for(const auto& f:q)faces.push_back({c[f[0]],c[f[1]],c[f[2]],c[f[3]]});
    area::Hull hull;area::make_hull(faces,false,hull);return hull;
}
bool near_x(nav::Vec3 p,float x) {return std::abs(p.x-x)<0.5f;}
// Total area of the pieces, and whether any piece lies in the plane x = value.
float total_area(const std::vector<area::Piece>& pieces) {
    float sum=0;
    for(const auto& piece:pieces)for(std::size_t i=1;i+1<piece.points.size();++i) {
        const auto n=area::cross(piece.points[i]-piece.points[0],piece.points[i+1]-piece.points[0]);sum+=0.5f*std::sqrt(nav::dot(n,n));
    }
    return sum;
}
bool piece_in_plane_x(const std::vector<area::Piece>& pieces,float x) {
    for(const auto& piece:pieces) {
        bool all=true;for(const auto p:piece.points)if(!near_x(p,x))all=false;
        if(all)return true;
    }
    return false;
}
bool seam_edge_at_x(const std::vector<area::Edge>& edges,float x) {
    for(const auto& e:edges)if(near_x(e.a,x)&&near_x(e.b,x))return true;
    return false;
}
}
int main() {
    std::vector<area::Piece> pieces;std::vector<area::Edge> edges;
    // Two 100-unit boxes touching at x = 100: one 200 x 100 x 100 box.
    area::build_surface({box({0,0,0},{100,100,100}),box({100,0,0},{200,100,100})},pieces,edges);
    check(!piece_in_plane_x(pieces,100),"touching boxes: shared face removed");
    check(!seam_edge_at_x(edges,100),"touching boxes: no seam edges");
    check(std::abs(total_area(pieces)-(2*200*100*2+100*100*2))<1,"touching boxes: area of one 200x100x100 box");
    check(edges.size()==16,"touching boxes: 12 box edges, the 4 long ones in two parts");
    // Overlapping boxes x 0..100 and 50..150: one 150 x 100 x 100 box.
    area::build_surface({box({0,0,0},{100,100,100}),box({50,0,0},{150,100,100})},pieces,edges);
    check(!piece_in_plane_x(pieces,50)&&!piece_in_plane_x(pieces,100),"overlapping boxes: inner faces removed");
    check(!seam_edge_at_x(edges,50)&&!seam_edge_at_x(edges,100),"overlapping boxes: no seam edges");
    check(std::abs(total_area(pieces)-(4*150*100+2*100*100))<1,"overlapping boxes: area of one 150x100x100 box");
    // Duplicated box: drawn once.
    area::build_surface({box({0,0,0},{100,100,100}),box({0,0,0},{100,100,100})},pieces,edges);
    check(std::abs(total_area(pieces)-6*100*100)<1,"duplicate boxes: surface drawn once");
    check(edges.size()==12,"duplicate boxes: 12 edges");
    // A step: the lower box's top meets the taller box's wall; that corner is a real edge.
    area::build_surface({box({0,0,0},{100,100,50}),box({100,0,0},{200,100,100})},pieces,edges);
    bool step=false;for(const auto& e:edges)if(near_x(e.a,100)&&near_x(e.b,100)&&std::abs(e.a.z-50)<0.5f&&std::abs(e.b.z-50)<0.5f)step=true;
    check(step,"step: the concave corner edge is kept");
    // Map-sized load: 400 volumes in a 20 x 20 field, each overlapping its neighbours.
    // The build runs off the render thread, but must still finish quickly.
    std::vector<area::Hull> field;
    for(int x=0;x<20;++x)for(int y=0;y<20;++y)field.push_back(box({x*90.0f,y*90.0f,0},{x*90.0f+100,y*90.0f+100,120}));
    const auto start=std::chrono::steady_clock::now();
    area::build_surface(field,pieces,edges);
    const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
    check(ms<1000,"400 overlapping volumes merge in under a second");
    check(std::abs(total_area(pieces)-(2*1810.0f*1810.0f+4*1810.0f*120.0f))<50,"400 volumes: area of the outer box");
    std::printf("Playable area merge checks: %s (%d checks, %d failures; 400 volumes in %lld ms)\n",failures?"FAIL":"PASS",checks,failures,static_cast<long long>(ms));
    return failures?1:0;
}
