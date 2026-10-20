#pragma once
#include "navigation.hpp"
#include <array>
namespace triggers {
struct Plane {nav::Vec3 normal;float distance=0;};
inline bool brush_list_address(std::uint32_t address,std::uint32_t count) {
 return count>0&&count<=64&&address>=0x10000&&(address&1u)==0&&static_cast<std::uint64_t>(address)+count*2ull<=0x100000000ull;
}
struct Mesh {std::vector<std::vector<nav::Vec3>> faces;};
inline nav::Vec3 cross(nav::Vec3 a,nav::Vec3 b) {return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline bool valid_bounds(nav::Vec3 lo,nav::Vec3 hi) {
 return nav::finite(lo)&&nav::finite(hi)&&lo.x<hi.x&&lo.y<hi.y&&lo.z<hi.z&&
  std::abs(lo.x)<=1000000&&std::abs(lo.y)<=1000000&&std::abs(lo.z)<=1000000&&
  std::abs(hi.x)<=1000000&&std::abs(hi.y)<=1000000&&std::abs(hi.z)<=1000000;
}
inline std::vector<Plane> axial_planes(nav::Vec3 lo,nav::Vec3 hi) {
 return {{{1,0,0},hi.x},{{-1,0,0},-lo.x},{{0,1,0},hi.y},{{0,-1,0},-lo.y},{{0,0,1},hi.z},{{0,0,-1},-lo.z}};
}
// Recover each convex brush as its actual intersection of axial/nonaxial planes.
inline bool convex_mesh(const std::vector<Plane>& planes,Mesh& out) {
 out.faces.clear();if(planes.size()<6||planes.size()>24)return false;
 for(const auto& plane:planes)if(!nav::finite(plane.normal)||!std::isfinite(plane.distance)||
  std::abs(plane.distance)>1000000||std::abs(nav::dot(plane.normal,plane.normal)-1)>0.02f)return false;
 std::vector<nav::Vec3> vertices;
 for(std::size_t i=0;i<planes.size();++i)for(std::size_t j=i+1;j<planes.size();++j)for(std::size_t k=j+1;k<planes.size();++k) {
  const auto bc=cross(planes[j].normal,planes[k].normal);const float determinant=nav::dot(planes[i].normal,bc);
  if(std::abs(determinant)<0.00001f)continue;
  const auto p=(bc*planes[i].distance+cross(planes[k].normal,planes[i].normal)*planes[j].distance+
   cross(planes[i].normal,planes[j].normal)*planes[k].distance)*(1/determinant);
  if(!nav::finite(p))return false;
  bool inside=true;for(const auto& plane:planes)if(nav::dot(plane.normal,p)>plane.distance+0.02f){inside=false;break;}
  if(!inside)continue;
  bool duplicate=false;for(const auto vertex:vertices)if(nav::dot(vertex-p,vertex-p)<0.0004f){duplicate=true;break;}
  if(!duplicate)vertices.push_back(p);if(vertices.size()>128)return false;
 }
 if(vertices.size()<4)return false;
 for(const auto& plane:planes) {
  std::vector<nav::Vec3> face;nav::Vec3 center{};
  for(const auto p:vertices)if(std::abs(nav::dot(plane.normal,p)-plane.distance)<0.03f){face.push_back(p);center=center+p;}
  if(face.size()<3)continue;center=center*(1/static_cast<float>(face.size()));
  auto u=cross(plane.normal,std::abs(plane.normal.z)<0.9f?nav::Vec3{0,0,1}:nav::Vec3{0,1,0});
  u=u*(1/std::sqrt(nav::dot(u,u)));const auto v=cross(plane.normal,u);
  std::sort(face.begin(),face.end(),[&](nav::Vec3 a,nav::Vec3 b){return std::atan2(nav::dot(a-center,v),nav::dot(a-center,u))<std::atan2(nav::dot(b-center,v),nav::dot(b-center,u));});
  out.faces.push_back(std::move(face));
 }
 return out.faces.size()>=4;
}
inline bool box_mesh(nav::Vec3 lo,nav::Vec3 hi,Mesh& out) {return valid_bounds(lo,hi)&&convex_mesh(axial_planes(lo,hi),out);}
// Analytic radius/height are native values; 32 chords approximate curved drawing.
inline bool cylinder_mesh(float radius,float height,Mesh& out) {
 out.faces.clear();if(!std::isfinite(radius)||!std::isfinite(height)||radius<=0||height<=0||radius>100000||height>100000)return false;
 constexpr unsigned segments=32;std::vector<nav::Vec3> bottom,top;
 for(unsigned i=0;i<segments;++i){const float a=6.28318530718f*i/segments;bottom.push_back({radius*std::cos(a),radius*std::sin(a),0});top.push_back({radius*std::cos(a),radius*std::sin(a),height});}
 for(unsigned i=0;i<segments;++i){const unsigned j=(i+1)%segments;out.faces.push_back({bottom[i],bottom[j],top[j],top[i]});}
 out.faces.push_back(std::move(bottom));out.faces.push_back(std::move(top));return true;
}
// Convex hull vs axis-aligned range box: separating axes include face normals,
// box axes and every hull-edge/box-edge cross product. Origin-only and hull
// enclosing-AABB tests cannot establish actual shape overlap.
template<class Transform> bool bounds_overlap(const Mesh& mesh,nav::Vec3 center,nav::Vec3 half,const Transform& world) {
 if(!nav::finite(center)||!nav::finite(half)||half.x<=0||half.y<=0||half.z<=0||mesh.faces.empty())return false;
 std::vector<std::vector<nav::Vec3>> faces;std::vector<nav::Vec3> vertices;
 for(const auto& face:mesh.faces) {
  std::vector<nav::Vec3> points;for(const auto p:face){const auto v=world(p)-center;if(!nav::finite(v))return false;points.push_back(v);vertices.push_back(v);}
  faces.push_back(std::move(points));
 }
 if(vertices.empty())return false;
 const auto separated=[&](nav::Vec3 axis) {
  const float length=std::sqrt(nav::dot(axis,axis));if(length<0.00001f)return false;axis=axis*(1/length);
  float lo=nav::dot(axis,vertices.front()),hi=lo;
  for(const auto p:vertices){const float d=nav::dot(axis,p);lo=std::min(lo,d);hi=std::max(hi,d);}
  const float extent=half.x*std::abs(axis.x)+half.y*std::abs(axis.y)+half.z*std::abs(axis.z);
  return lo>extent+0.001f||hi< -extent-0.001f;
 };
 const nav::Vec3 axes[]={{1,0,0},{0,1,0},{0,0,1}};
 for(const auto axis:axes)if(separated(axis))return false;
 for(const auto& face:faces) {
  if(face.size()<3)return false;
  if(separated(cross(face[1]-face[0],face[2]-face[0])))return false;
  for(std::size_t i=0;i<face.size();++i)for(const auto axis:axes)if(separated(cross(face[(i+1)%face.size()]-face[i],axis)))return false;
 }
 return true;
}
template<class Transform> bool range_overlap(const Mesh& mesh,nav::Vec3 center,float horizontal,float vertical,const Transform& world) {
 return bounds_overlap(mesh,center,{horizontal,horizontal,vertical},world);
}
inline bool cylinder_bounds_overlap(nav::Vec3 origin,float radius,float height,nav::Vec3 lo,nav::Vec3 hi) {
 if(!nav::finite(origin)||!valid_bounds(lo,hi)||!std::isfinite(radius)||!std::isfinite(height)||radius<=0||height<=0)return false;
 if(origin.z>hi.z||origin.z+height<lo.z)return false;
 const float dx=origin.x-std::max(lo.x,std::min(hi.x,origin.x)),dy=origin.y-std::max(lo.y,std::min(hi.y,origin.y));
 return dx*dx+dy*dy<=radius*radius;
}
inline bool cylinder_range_overlap(nav::Vec3 origin,float radius,float height,nav::Vec3 center,float horizontal,float vertical) {
 if(!nav::finite(origin)||!nav::finite(center)||!std::isfinite(radius)||!std::isfinite(height)||!std::isfinite(horizontal)||!std::isfinite(vertical)||radius<=0||height<=0||horizontal<=0||vertical<=0)return false;
 if(origin.z>center.z+vertical||origin.z+height<center.z-vertical)return false;
 const float dx=std::max(0.0f,std::abs(origin.x-center.x)-horizontal),dy=std::max(0.0f,std::abs(origin.y-center.y)-horizontal);
 return dx*dx+dy*dy<=radius*radius;
}
inline bool radial_bounds(nav::Vec3 lo,nav::Vec3 hi) {
 return valid_bounds(lo,hi)&&lo.z==0&&hi.x==hi.y&&lo.x==-hi.x&&lo.y==-hi.y;
}
}
