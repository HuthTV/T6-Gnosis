#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "memory.hpp"
#include "world_collision.hpp"
#include <atomic>
#include <cstring>
#include <mutex>
#include <set>
#include "triggers.hpp"
#include "trigger_geometry.hpp"

namespace collision {
namespace {
std::mutex guard;
std::atomic<bool> enabled{false};
std::shared_ptr<const Snapshot> current;
const char* state="Off";
struct Roots {
    std::uint32_t name=0,planes=0,plane_count=0,sides=0,side_count=0,nodes=0,node_count=0,
        brushes=0,verts=0,vert_count=0,tris=0,tri_count=0,models=0,model_count=0,brush_count=0;
    // All 32-bit fields, so there is no padding and byte comparison is exact.
    bool operator==(const Roots& o) const {return std::memcmp(this,&o,sizeof(Roots))==0;}
};
static_assert(sizeof(Roots)==15*4);
Roots built_roots{};
bool read(std::uint32_t address,void* out,std::size_t size) {
    if(address<0x10000||size>0x800000||size>0xFFFFFFFFu-address)return false;
    return memory::read(address,out,size);
}
template<class T> bool read(std::uint32_t address,T& out) {return read(address,&out,sizeof(out));}
template<class T> T field(const unsigned char* data,std::size_t at) {T v{};std::memcpy(&v,data+at,sizeof(v));return v;}
bool array_fits(std::uint32_t base,std::uint32_t count,std::uint32_t stride) {
    return base>=0x10000&&static_cast<std::uint64_t>(base)+static_cast<std::uint64_t>(count)*stride<=0x100000000ull;
}
// clipMap fields at 0x2503880 (see world_collision.hpp); counts bounded to sane sizes.
bool roots(Roots& r,const std::string& map) {
    r=Roots{};char name[160]{};std::uint16_t brush_count=0;
    if(!read(0x2503880,r.name)||!read(r.name,name,sizeof(name)-1)||map!=name)return false;
    if(!read(0x25038C0,brush_count))return false;
    r.brush_count=brush_count;
    return read(0x2503888,r.plane_count)&&read(0x250388C,r.planes)&&r.plane_count>0&&r.plane_count<=1000000&&array_fits(r.planes,r.plane_count,20)&&
        read(0x2503898,r.side_count)&&read(0x250389C,r.sides)&&r.side_count<=1000000&&(r.side_count==0||array_fits(r.sides,r.side_count,12))&&
        read(0x25038A0,r.node_count)&&read(0x25038A4,r.nodes)&&r.node_count>0&&r.node_count<=200000&&array_fits(r.nodes,r.node_count,20)&&
        read(0x25038C4,r.brushes)&&r.brush_count>0&&array_fits(r.brushes,r.brush_count,96)&&
        read(0x25038EC,r.vert_count)&&read(0x25038F0,r.verts)&&r.vert_count<=65536&&(r.vert_count==0||array_fits(r.verts,r.vert_count,12))&&
        read(0x25038F4,r.tri_count)&&read(0x25038F8,r.tris)&&r.tri_count<=500000&&(r.tri_count==0||array_fits(r.tris,r.tri_count,6))&&
        read(0x2503910,r.model_count)&&read(0x2503914,r.models)&&r.model_count>0&&r.model_count<=16384&&array_fits(r.models,r.model_count,0x4C);
}
// Brushes owned by entity cmodels (local space): same leaf-brush-node walk as
// zombies.cpp leaf_brushes, over the copied node array, without its 32-brush cap.
bool entity_walk(const std::vector<unsigned char>& nodes,std::uint32_t count,std::uint32_t brush_count,std::uint32_t index,unsigned depth,std::set<std::uint32_t>& seen,std::vector<bool>& owned) {
    if(index>=count||depth>64||seen.size()>200000)return false;
    if(!seen.insert(index).second)return true;
    const unsigned char* n=nodes.data()+index*20u;
    const auto leaf=field<std::int16_t>(n,2);
    if(leaf>0) {
        if(leaf>1024)return false;
        std::vector<std::uint16_t> ids(static_cast<std::size_t>(leaf));
        if(!read(field<std::uint32_t>(n,8),ids.data(),ids.size()*2))return false;
        for(const auto id:ids){if(id>=brush_count)return false;owned[id]=true;}
        return true;
    }
    if(leaf<0&&!entity_walk(nodes,count,brush_count,index+1,depth+1,seen,owned))return false;
    for(unsigned side=0;side<2;++side) {
        const auto child=field<std::uint16_t>(n,16+side*2);
        if(child&&!entity_walk(nodes,count,brush_count,index+child,depth+1,seen,owned))return false;
    }
    return true;
}
// Triangles owned by entity cmodels. Each cmodel's leaf (cmodel +0x20) lists its
// AABB trees: u16 first tree +0x20, u16 tree count +0x22. Interior nodes (u16 child
// count +14) hold their first child at +28, leaves a partition. The world's own
// leaf (cmodel 0) lists none. Origins: 644 nodraw triangles in cmodels 8-197, stored
// in model-local space (they drew as floating slabs around the map origin).
bool tree_walk(const std::vector<unsigned char>& tree,const std::vector<unsigned char>& parts,std::uint32_t index,unsigned depth,std::vector<bool>& owned) {
    const auto nodes=static_cast<std::uint32_t>(tree.size()/32),partitions=static_cast<std::uint32_t>(parts.size()/16);
    if(index>=nodes||depth>64)return false;
    const unsigned char* n=tree.data()+index*32u;
    const auto children=field<std::uint16_t>(n,14);const auto u=field<std::uint32_t>(n,28);
    if(!children) {
        if(u>=partitions)return false;
        const unsigned count=parts[u*16u];const auto first=field<std::int32_t>(parts.data()+u*16u,4);
        if(first<0||static_cast<std::uint64_t>(first)+count>owned.size())return false;
        for(unsigned t=0;t<count;++t)owned[static_cast<std::size_t>(first)+t]=true;
        return true;
    }
    for(unsigned k=0;k<children;++k)if(!tree_walk(tree,parts,u+k,depth+1,owned))return false;
    return true;
}
bool entity_triangles(const std::vector<unsigned char>& models,std::uint32_t model_count,std::uint32_t tri_count,std::vector<bool>& owned) {
    std::uint32_t pc=0,pp=0,ac=0,ap=0;
    if(!read(0x2503900,pc)||!read(0x2503904,pp)||!read(0x2503908,ac)||!read(0x250390C,ap))return false;
    if(!pc||pc>1000000||!ac||ac>2000000||!array_fits(pp,pc,16)||!array_fits(ap,ac,32))return false;
    std::vector<unsigned char> parts(pc*16u),tree(ac*32u);
    if(!read(pp,parts.data(),parts.size())||!read(ap,tree.data(),tree.size()))return false;
    owned.assign(tri_count,false);
    for(std::uint32_t m=1;m<model_count;++m) {
        const unsigned char* c=models.data()+m*0x4Cu;
        const auto first=field<std::uint16_t>(c,0x20),count=field<std::uint16_t>(c,0x22);
        for(unsigned k=0;k<count;++k)if(!tree_walk(tree,parts,first+k,0,owned))return false;
    }
    return true;
}
void add_face(Snapshot& s,const std::vector<nav::Vec3>& polygon,nav::Vec3 normal,Kind kind,bool triangle,bool blocking,std::uint32_t source,std::uint32_t contents,int material) {
    nav::Vec3 centre{},lo=polygon[0],hi=polygon[0];
    for(const auto p:polygon) {
        centre=centre+p;
        lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};
    }
    centre=centre*(1.0f/static_cast<float>(polygon.size()));
    const auto index=static_cast<std::uint32_t>(s.faces.size());
    const auto x0=static_cast<std::int64_t>(std::floor(lo.x/cell_size)),x1=static_cast<std::int64_t>(std::floor(hi.x/cell_size));
    const auto y0=static_cast<std::int64_t>(std::floor(lo.y/cell_size)),y1=static_cast<std::int64_t>(std::floor(hi.y/cell_size));
    if((x1-x0+1)*(y1-y0+1)>64)s.large.push_back(index);
    else for(auto ix=x0;ix<=x1;++ix)for(auto iy=y0;iy<=y1;++iy)
        s.cells[cell_key((static_cast<float>(ix)+0.5f)*cell_size,(static_cast<float>(iy)+0.5f)*cell_size)].push_back(index);
    s.faces.push_back({polygon,normal,centre,lo,hi,kind,triangle,blocking,source,contents,material});
}
// Assigns every collision triangle its material (AABB-tree leaf -> partition ->
// triangle range). Any inconsistency rejects the whole mapping.
bool map_materials(std::uint32_t tri_count,std::vector<int>& material,std::vector<std::string>& names,std::vector<std::uint32_t>& contents,std::string& status) {
    std::uint32_t mc=0,mp=0,pc=0,pp=0,ac=0,ap=0;
    if(!read(0x2503890,mc)||!read(0x2503894,mp)||!read(0x2503900,pc)||!read(0x2503904,pp)||!read(0x2503908,ac)||!read(0x250390C,ap)) {status="material fields unreadable";return false;}
    if(!mc||mc>4096||!pc||pc>1000000||!ac||ac>2000000||!array_fits(mp,mc,12)||!array_fits(pp,pc,16)||!array_fits(ap,ac,32)) {status="material counts out of range";return false;}
    std::vector<unsigned char> mats(mc*12u),parts(pc*16u),tree(ac*32u);
    if(!read(mp,mats.data(),mats.size())||!read(pp,parts.data(),parts.size())||!read(ap,tree.data(),tree.size())) {status="material arrays unreadable";return false;}
    std::uint64_t sum=0;
    for(std::uint32_t i=0;i<pc;++i) {
        const unsigned count=parts[i*16u];const auto first=field<std::int32_t>(parts.data()+i*16u,4);
        if(first<0||static_cast<std::uint64_t>(first)+count>tri_count) {status="partition out of range";return false;}
        sum+=count;
    }
    if(sum!=tri_count) {status="partition triangle total "+std::to_string(sum)+" != "+std::to_string(tri_count);return false;}
    material.assign(tri_count,-1);
    for(std::uint32_t i=0;i<ac;++i) {
        const unsigned char* node=tree.data()+i*32u;
        if(field<std::uint16_t>(node,14))continue;  // interior node
        const auto m=field<std::uint16_t>(node,12);const auto partition=field<std::uint32_t>(node,28);
        if(m>=mc||partition>=pc) {status="tree leaf out of range";return false;}
        const unsigned count=parts[partition*16u];const auto first=static_cast<std::uint32_t>(field<std::int32_t>(parts.data()+partition*16u,4));
        for(std::uint32_t t=first;t<first+count;++t) {
            if(material[t]>=0&&material[t]!=m) {status="triangle with two materials";return false;}
            material[t]=m;
        }
    }
    // A few triangles belong to no leaf (15 of 67196 on Origins): keep them blocking.
    names.assign(mc,{});contents.assign(mc,0);
    for(std::uint32_t i=0;i<mc;++i) {
        contents[i]=field<std::uint32_t>(mats.data()+i*12u,8);
        char name[64]{};if(read(field<std::uint32_t>(mats.data()+i*12u,0),name,sizeof(name)-1))names[i]=name;
    }
    status="ok";return true;
}
std::shared_ptr<const Snapshot> build(const Roots& r,const std::string& map) {
    auto s=std::make_shared<Snapshot>();s->map=map;
    std::vector<unsigned char> brushes(r.brush_count*96u),nodes(r.node_count*20u),models(r.model_count*0x4Cu),sides(r.side_count*12u),planes(r.plane_count*20u);
    if(!read(r.brushes,brushes.data(),brushes.size())||!read(r.nodes,nodes.data(),nodes.size())||!read(r.models,models.data(),models.size())||
       !read(r.planes,planes.data(),planes.size())||(r.side_count&&!read(r.sides,sides.data(),sides.size())))return nullptr;
    std::vector<bool> owned(r.brush_count,false);
    for(std::uint32_t m=1;m<r.model_count;++m) {
        const auto node=field<std::int32_t>(models.data()+m*0x4Cu,0x44);
        std::set<std::uint32_t> seen;
        if(node<0||!entity_walk(nodes,r.node_count,r.brush_count,static_cast<std::uint32_t>(node),0,seen,owned))return nullptr;
    }
    for(std::uint32_t id=0;id<r.brush_count;++id) {
        if(owned[id]) {++s->entity_brushes;continue;}
        const unsigned char* b=brushes.data()+id*96u;
        const auto contents=field<std::uint32_t>(b,12);
        if(!(contents&0x10001u)) {++s->other_contents;continue;}
        const auto lo=field<nav::Vec3>(b,0),hi=field<nav::Vec3>(b,16);
        const auto side_count=field<std::uint32_t>(b,28),side_ptr=field<std::uint32_t>(b,32);
        if(!triggers::valid_bounds(lo,hi)||side_count>18) {++s->skipped_brushes;continue;}
        auto brush_planes=triggers::axial_planes(lo,hi);bool ok=true;
        for(std::uint32_t i=0;i<side_count&&ok;++i) {
            const auto side=side_ptr+i*12u;
            ok=side>=r.sides&&(side-r.sides)%12==0&&(side-r.sides)/12<r.side_count;
            if(!ok)break;
            const auto plane=field<std::uint32_t>(sides.data()+(side-r.sides),0);
            ok=plane>=r.planes&&(plane-r.planes)%20==0&&(plane-r.planes)/20<r.plane_count;
            if(!ok)break;
            const unsigned char* p=planes.data()+(plane-r.planes);
            brush_planes.push_back({field<nav::Vec3>(p,0),field<float>(p,12)});
        }
        triggers::Mesh mesh;
        if(!ok||!triggers::convex_mesh(brush_planes,mesh)) {++s->skipped_brushes;continue;}
        ++s->world_brushes;
        const bool clip_only=!(contents&1u);
        for(const auto& face:mesh.faces) {
            const auto n=triggers::cross(face[1]-face[0],face[2]-face[0]);const float l=std::sqrt(nav::dot(n,n));
            if(l<1e-6f)continue;
            const auto normal=n*(1/l);
            add_face(*s,face,normal,clip_only?Kind::clip:orientation(normal),false,true,id,contents,-1);
        }
    }
    if(r.tri_count) {
        std::vector<nav::Vec3> verts(r.vert_count);std::vector<std::uint16_t> tris(r.tri_count*3u);
        if(!read(r.verts,verts.data(),verts.size()*12)||!read(r.tris,tris.data(),tris.size()*2))return nullptr;
        for(const auto& v:verts)if(!nav::finite(v)||std::abs(v.x)>1000000||std::abs(v.y)>1000000||std::abs(v.z)>1000000)return nullptr;
        std::vector<int> material;std::vector<std::string> names;std::vector<std::uint32_t> contents;
        s->materials_ok=map_materials(r.tri_count,material,names,contents,s->material_status);
        if(s->materials_ok)s->material_names=names;
        std::vector<unsigned> skipped_by_material(names.size(),0);
        // Entity-owned triangles are in model-local space: never draw them as world.
        std::vector<bool> owned_tris;
        if(!entity_triangles(models,r.model_count,r.tri_count,owned_tris))return nullptr;
        for(std::uint32_t t=0;t<r.tri_count;++t) {
            const auto ia=tris[t*3],ib=tris[t*3+1],ic=tris[t*3+2];
            if(ia>=r.vert_count||ib>=r.vert_count||ic>=r.vert_count)return nullptr;
            if(owned_tris[t]) {++s->entity_triangles;continue;}
            const std::vector<nav::Vec3> polygon{verts[ia],verts[ib],verts[ic]};
            // Collision triangles wind clockwise: the floor the player stood on
            // (tri 46805) has cross(B-A,C-A) pointing down. Negate for the outward normal.
            const auto n=triggers::cross(polygon[2]-polygon[0],polygon[1]-polygon[0]);const float l=std::sqrt(nav::dot(n,n));
            if(l<1e-6f)continue;
            // Same player rule as brushes: SOLID (0x1) or PLAYERCLIP (0x10000).
            bool blocking=true;
            if(s->materials_ok&&material[t]>=0) {
                const auto m=static_cast<std::size_t>(material[t]);blocking=(contents[m]&0x10001u)!=0;
                if(!blocking){++skipped_by_material[m];++s->nonblocking_triangles;}
            }
            add_face(*s,polygon,n*(1/l),orientation(n*(1/l)),true,blocking,t,s->materials_ok&&material[t]>=0?contents[static_cast<std::size_t>(material[t])]:0u,s->materials_ok?material[t]:-1);++s->triangles;
        }
        for(std::size_t m=0;m<skipped_by_material.size();++m)
            if(skipped_by_material[m])s->nonblocking.push_back({names[m],contents[m],skipped_by_material[m]});
        std::sort(s->nonblocking.begin(),s->nonblocking.end(),[](const MaterialUse& a,const MaterialUse& b){return a.triangles>b.triangles;});
    }
    // The build is only kept if every root is unchanged afterwards.
    Roots after{};if(!roots(after,map)||!(after==r))return nullptr;
    return s;
}
}
void enable(bool on) {enabled=on;}
void update() {
    if(!enabled.load()) {std::lock_guard lock(guard);state="Off";return;}
    if(!triggers::available()) {std::lock_guard lock(guard);current.reset();state="Collision bindings unavailable (fingerprint mismatch)";return;}
    const auto g=nav::graph();Roots r{};
    if(!g||!roots(r,g->map)) {std::lock_guard lock(guard);current.reset();built_roots={};state="Waiting for a loaded map";return;}
    {std::lock_guard lock(guard);if(current&&r==built_roots)return;}
    auto s=build(r,g->map);
    std::lock_guard lock(guard);
    current=s;built_roots=s?r:Roots{};state=s?"Ready":"Collision data rejected; nothing drawn";
}
void clear() {std::lock_guard lock(guard);current.reset();built_roots={};}
std::shared_ptr<const Snapshot> snapshot() {std::lock_guard lock(guard);return current;}
const char* status() {std::lock_guard lock(guard);return state;}
}
