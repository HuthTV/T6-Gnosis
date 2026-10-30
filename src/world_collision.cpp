#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "memory.hpp"
#include "world_collision.hpp"
#include <atomic>
#include <algorithm>
#include <cstring>
#include <mutex>
#include <set>
#include "triggers.hpp"
#include "trigger_geometry.hpp"
#include "zombies.hpp"
#include <array>

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
bool entity_walk(const std::vector<unsigned char>& nodes,std::uint32_t count,std::uint32_t brush_count,std::uint32_t index,unsigned depth,std::set<std::uint32_t>& seen,std::vector<bool>& owned,std::set<std::uint32_t>& found) {
    if(index>=count||depth>64||seen.size()>200000)return false;
    if(!seen.insert(index).second)return true;
    const unsigned char* n=nodes.data()+index*20u;
    const auto leaf=field<std::int16_t>(n,2);
    if(leaf>0) {
        if(leaf>1024)return false;
        std::vector<std::uint16_t> ids(static_cast<std::size_t>(leaf));
        if(!read(field<std::uint32_t>(n,8),ids.data(),ids.size()*2))return false;
        for(const auto id:ids){if(id>=brush_count)return false;owned[id]=true;found.insert(id);}
        return true;
    }
    if(leaf<0&&!entity_walk(nodes,count,brush_count,index+1,depth+1,seen,owned,found))return false;
    for(unsigned side=0;side<2;++side) {
        const auto child=field<std::uint16_t>(n,16+side*2);
        if(child&&!entity_walk(nodes,count,brush_count,index+child,depth+1,seen,owned,found))return false;
    }
    return true;
}
// Triangles owned by entity cmodels. Each cmodel's leaf (cmodel +0x20) lists its
// AABB trees: u16 first tree +0x20, u16 tree count +0x22. Interior nodes (u16 child
// count +14) hold their first child at +28, leaves a partition. The world's own
// leaf (cmodel 0) lists none. Origins: 644 nodraw triangles in cmodels 8-197, stored
// in model-local space (they drew as floating slabs around the map origin).
bool tree_walk(const std::vector<unsigned char>& tree,const std::vector<unsigned char>& parts,std::uint32_t index,unsigned depth,std::vector<bool>& owned,std::vector<std::uint32_t>& found) {
    const auto nodes=static_cast<std::uint32_t>(tree.size()/32),partitions=static_cast<std::uint32_t>(parts.size()/16);
    if(index>=nodes||depth>64)return false;
    const unsigned char* n=tree.data()+index*32u;
    const auto children=field<std::uint16_t>(n,14);const auto u=field<std::uint32_t>(n,28);
    if(!children) {
        if(u>=partitions)return false;
        const unsigned count=parts[u*16u];const auto first=field<std::int32_t>(parts.data()+u*16u,4);
        if(first<0||static_cast<std::uint64_t>(first)+count>owned.size())return false;
        for(unsigned t=0;t<count;++t) {owned[static_cast<std::size_t>(first)+t]=true;found.push_back(static_cast<std::uint32_t>(first)+t);}
        return true;
    }
    for(unsigned k=0;k<children;++k)if(!tree_walk(tree,parts,u+k,depth+1,owned,found))return false;
    return true;
}
// per_model[m] lists the triangles cmodel m owns.
bool entity_triangles(const std::vector<unsigned char>& models,std::uint32_t model_count,std::uint32_t tri_count,std::vector<bool>& owned,std::vector<std::vector<std::uint32_t>>& per_model) {
    std::uint32_t pc=0,pp=0,ac=0,ap=0;
    if(!read(0x2503900,pc)||!read(0x2503904,pp)||!read(0x2503908,ac)||!read(0x250390C,ap))return false;
    if(!pc||pc>1000000||!ac||ac>2000000||!array_fits(pp,pc,16)||!array_fits(ap,ac,32))return false;
    std::vector<unsigned char> parts(pc*16u),tree(ac*32u);
    if(!read(pp,parts.data(),parts.size())||!read(ap,tree.data(),tree.size()))return false;
    owned.assign(tri_count,false);per_model.assign(model_count,{});
    for(std::uint32_t m=1;m<model_count;++m) {
        const unsigned char* c=models.data()+m*0x4Cu;
        const auto first=field<std::uint16_t>(c,0x20),count=field<std::uint16_t>(c,0x22);
        for(unsigned k=0;k<count;++k)if(!tree_walk(tree,parts,first+k,0,owned,per_model[m]))return false;
    }
    return true;
}
// A face without the spatial grid (entity cmodel faces, kept in model-local space).
Face local_face(const std::vector<nav::Vec3>& polygon,nav::Vec3 normal,Kind kind,bool triangle,bool blocking,std::uint32_t source,std::uint32_t contents,int material) {
    Face f{polygon,normal,{},polygon[0],polygon[0],kind,triangle,blocking,source,contents,material};
    for(const auto p:polygon) {
        f.centre=f.centre+p;
        f.lo={std::min(f.lo.x,p.x),std::min(f.lo.y,p.y),std::min(f.lo.z,p.z)};f.hi={std::max(f.hi.x,p.x),std::max(f.hi.y,p.y),std::max(f.hi.z,p.z)};
    }
    f.centre=f.centre*(1.0f/static_cast<float>(polygon.size()));
    return f;
}
// Live brush-model entities whose cmodel has faces (refreshed in update, posed per frame).
struct Mover {std::uint16_t number=0;std::int16_t handle=0;};
std::vector<Mover> movers_found;
std::vector<std::uint16_t> model_entities;  // entity numbers of 3D-model entities, refreshed in update
constexpr std::uint32_t entity_pointer=0x2338784,entity_count=0x233878C,entity_base=0x21C13C0,entity_size=0x31C;
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
    std::vector<std::set<std::uint32_t>> model_brushes(r.model_count);
    for(std::uint32_t m=1;m<r.model_count;++m) {
        const auto node=field<std::int32_t>(models.data()+m*0x4Cu,0x44);
        std::set<std::uint32_t> seen;
        if(node<0||!entity_walk(nodes,r.node_count,r.brush_count,static_cast<std::uint32_t>(node),0,seen,owned,model_brushes[m]))return nullptr;
    }
    // Faces of one player-blocking brush (bounds plus side planes); false when the brush
    // blocks nothing or does not decode.
    enum class Decoded {ok,other_contents,skipped};
    auto brush_faces=[&](std::uint32_t id,auto&& emit)->Decoded {
        const unsigned char* b=brushes.data()+id*96u;
        const auto contents=field<std::uint32_t>(b,12);
        if(!(contents&0x10001u))return Decoded::other_contents;
        const auto lo=field<nav::Vec3>(b,0),hi=field<nav::Vec3>(b,16);
        const auto side_count=field<std::uint32_t>(b,28),side_ptr=field<std::uint32_t>(b,32);
        if(!triggers::valid_bounds(lo,hi)||side_count>18)return Decoded::skipped;
        auto brush_planes=triggers::axial_planes(lo,hi);
        for(std::uint32_t i=0;i<side_count;++i) {
            const auto side=side_ptr+i*12u;
            if(side<r.sides||(side-r.sides)%12||(side-r.sides)/12>=r.side_count)return Decoded::skipped;
            const auto plane=field<std::uint32_t>(sides.data()+(side-r.sides),0);
            if(plane<r.planes||(plane-r.planes)%20||(plane-r.planes)/20>=r.plane_count)return Decoded::skipped;
            const unsigned char* p=planes.data()+(plane-r.planes);
            brush_planes.push_back({field<nav::Vec3>(p,0),field<float>(p,12)});
        }
        triggers::Mesh mesh;
        if(!triggers::convex_mesh(brush_planes,mesh))return Decoded::skipped;
        const bool clip_only=!(contents&1u);
        for(const auto& face:mesh.faces) {
            const auto n=triggers::cross(face[1]-face[0],face[2]-face[0]);const float l=std::sqrt(nav::dot(n,n));
            if(l<1e-6f)continue;
            const auto normal=n*(1/l);
            emit(face,normal,clip_only?Kind::clip:orientation(normal),contents);
        }
        return Decoded::ok;
    };
    for(std::uint32_t id=0;id<r.brush_count;++id) {
        if(owned[id]) {++s->entity_brushes;continue;}
        const auto result=brush_faces(id,[&](const std::vector<nav::Vec3>& face,nav::Vec3 normal,Kind kind,std::uint32_t contents) {
            add_face(*s,face,normal,kind,false,true,id,contents,-1);
        });
        if(result==Decoded::ok)++s->world_brushes;else if(result==Decoded::other_contents)++s->other_contents;else ++s->skipped_brushes;
    }
    // Entity cmodels keep their faces in model-local space; movers() poses them at the
    // entity each frame. Kinds are classified again after rotation.
    s->models.assign(r.model_count,{});
    for(std::uint32_t m=1;m<r.model_count;++m)
        for(const auto id:model_brushes[m])
            brush_faces(id,[&](const std::vector<nav::Vec3>& face,nav::Vec3 normal,Kind kind,std::uint32_t contents) {
                s->models[m].push_back(local_face(face,normal,kind,false,true,id,contents,-1));
            });
    if(r.tri_count) {
        std::vector<nav::Vec3> verts(r.vert_count);std::vector<std::uint16_t> tris(r.tri_count*3u);
        if(!read(r.verts,verts.data(),verts.size()*12)||!read(r.tris,tris.data(),tris.size()*2))return nullptr;
        for(const auto& v:verts)if(!nav::finite(v)||std::abs(v.x)>1000000||std::abs(v.y)>1000000||std::abs(v.z)>1000000)return nullptr;
        std::vector<int> material;std::vector<std::string> names;std::vector<std::uint32_t> contents;
        s->materials_ok=map_materials(r.tri_count,material,names,contents,s->material_status);
        if(s->materials_ok)s->material_names=names;
        std::vector<unsigned> skipped_by_material(names.size(),0);
        // Entity-owned triangles are in model-local space: never draw them as world.
        std::vector<bool> owned_tris;std::vector<std::vector<std::uint32_t>> model_tris;
        if(!entity_triangles(models,r.model_count,r.tri_count,owned_tris,model_tris))return nullptr;
        for(std::uint32_t m=1;m<r.model_count;++m)
            for(const auto t:model_tris[m]) {
                const auto ia=tris[t*3],ib=tris[t*3+1],ic=tris[t*3+2];
                if(ia>=r.vert_count||ib>=r.vert_count||ic>=r.vert_count)return nullptr;
                const std::vector<nav::Vec3> polygon{verts[ia],verts[ib],verts[ic]};
                const auto n=triggers::cross(polygon[2]-polygon[0],polygon[1]-polygon[0]);const float l=std::sqrt(nav::dot(n,n));
                if(l<1e-6f)continue;
                const bool mapped=s->materials_ok&&material[t]>=0;
                const auto tri_contents=mapped?contents[static_cast<std::size_t>(material[t])]:0u;
                if(mapped&&!(tri_contents&0x10001u))continue;
                s->models[m].push_back(local_face(polygon,n*(1/l),orientation(n*(1/l)),true,true,t,tri_contents,mapped?material[t]:-1));
            }
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
    // Static props' bounds, for the model mask (a failed read leaves none: nothing breaks).
    {
        std::uint32_t count=0,base=0;
        if(read(0x25038D4,count)&&read(0x25038D8,base)&&count>0&&count<=65536&&array_fits(base,count,84)) {
            std::vector<unsigned char> raw(count*84u);
            if(read(base,raw.data(),raw.size()))
                for(std::uint32_t i=0;i<count;++i) {
                    const auto lo=field<nav::Vec3>(raw.data()+i*84u,60),hi=field<nav::Vec3>(raw.data()+i*84u,72);
                    if(triggers::valid_bounds(lo,hi)&&hi.x-lo.x<4096&&hi.y-lo.y<4096&&hi.z-lo.z<4096)s->props.push_back({lo,hi});
                }
        }
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
    std::shared_ptr<const Snapshot> s;
    {std::lock_guard lock(guard);if(current&&r==built_roots)s=current;}
    if(!s) {
        s=build(r,g->map);
        std::lock_guard lock(guard);
        current=s;built_roots=s?r:Roots{};state=s?"Ready":"Collision data rejected; nothing drawn";
    }
    // Brush-model entities whose cmodel has faces; movers() checks and poses them each frame.
    std::vector<Mover> found;std::vector<std::uint16_t> modelled;std::uint32_t base=0,count=0,table=0;
    if(s&&read(entity_pointer,base)&&base==entity_base&&read(entity_count,count)&&count>0&&count<=1024&&read(0x2BF2DA4,table)&&table) {
        std::vector<unsigned char> entities(count*entity_size);
        if(read(entity_base,entities.data(),entities.size()))
            for(std::uint32_t id=0;id<count;++id) {
                const unsigned char* e=entities.data()+id*entity_size;
                if(e[0xFB]==1&&e[0xF9]!=1&&field<std::uint32_t>(e,0)==id) {
                    char name[32]{};const auto token=field<std::uint16_t>(e,0x176);
                    // Zombie corpses (entity type 18, as zombies::living_entity tells them
                    // apart) lie on the floor and must cover collision too (user screenshot 2026-10-30).
                    if(field<std::uint16_t>(e,0xD8)==18||(token&&read(table+token*24u+4u,name,sizeof(name)-1)&&
                       (!std::strcmp(name,"script_model")||!std::strcmp(name,"script_vehicle")||!std::strncmp(name,"zbarrier_",9))))
                        modelled.push_back(static_cast<std::uint16_t>(id));
                }
                const auto handle=field<std::int16_t>(e,0xDC);
                if(e[0xFB]==1&&e[0xF9]==1&&field<std::uint32_t>(e,0)==id&&handle>0&&static_cast<std::size_t>(handle)<s->models.size()&&!s->models[static_cast<std::size_t>(handle)].empty())
                    found.push_back({static_cast<std::uint16_t>(id),handle});
            }
    }
    std::lock_guard lock(guard);movers_found=std::move(found);model_entities=std::move(modelled);
}
void model_boxes(const Snapshot& s,nav::Vec3 eye,float radius,std::size_t limit,std::vector<ModelBox>& out) {
    out.clear();if(!limit)return;
    auto add=[&](nav::Vec3 lo,nav::Vec3 hi) {
        const float dx=std::max({lo.x-eye.x,0.0f,eye.x-hi.x}),dy=std::max({lo.y-eye.y,0.0f,eye.y-hi.y}),dz=std::max({lo.z-eye.z,0.0f,eye.z-hi.z});
        const float d=std::sqrt(dx*dx+dy*dy+dz*dz);
        if(d<=radius)out.push_back({lo,hi,d});
    };
    for(const auto& p:s.props)add(p.first,p.second);
    std::vector<std::uint16_t> list;{std::lock_guard lock(guard);list=model_entities;}
    for(const auto number:list) {
        std::array<unsigned char,entity_size> e{};
        if(!read(entity_base+number*entity_size,e.data(),e.size())||e[0xFB]!=1||e[0xF9]==1||field<std::uint32_t>(e.data(),0)!=number)continue;
        const auto lo=field<nav::Vec3>(e.data(),0x11C),hi=field<nav::Vec3>(e.data(),0x128);
        if(triggers::valid_bounds(lo,hi)&&hi.x-lo.x<4096&&hi.y-lo.y<4096&&hi.z-lo.z<4096)add(lo,hi);
    }
    if(out.size()>limit) {
        std::nth_element(out.begin(),out.begin()+static_cast<std::ptrdiff_t>(limit),out.end(),[](const ModelBox& a,const ModelBox& b){return a.distance<b.distance;});
        out.resize(limit);
    }
}
unsigned movers(const Snapshot& s,nav::Vec3 eye,float radius,std::vector<Face>& out,bool model_bounds) {
    std::vector<Mover> list;std::vector<std::uint16_t> modelled;{std::lock_guard lock(guard);list=movers_found;if(model_bounds)modelled=model_entities;}
    unsigned posed=0;
    for(const auto& m:list) {
        // Same entity checks as at listing, plus the contents the engine traces against:
        // notsolid entities have none (live TranZit 2026-10-30: blockers 0x08030200 with
        // player clip, hidden reactor plates 0).
        std::array<unsigned char,entity_size> e{};
        if(!read(entity_base+m.number*entity_size,e.data(),e.size())||e[0xFB]!=1||e[0xF9]!=1||field<std::uint32_t>(e.data(),0)!=m.number||
           field<std::int16_t>(e.data(),0xDC)!=m.handle||static_cast<std::size_t>(m.handle)>=s.models.size()||!(field<std::uint32_t>(e.data(),0x118)&0x10001u))continue;
        const auto origin=field<nav::Vec3>(e.data(),0x134),angles=field<nav::Vec3>(e.data(),0x140);
        if(!nav::finite(origin)||!nav::finite(angles)||std::abs(origin.x)>1000000||std::abs(origin.y)>1000000||std::abs(origin.z)>1000000)continue;
        const auto d=origin-eye;
        if(d.x*d.x+d.y*d.y>(radius+2048)*(radius+2048))continue;
        // The engine traces against a cmodel in its entity's frame: origin + rotated local point.
        for(const auto& local:s.models[static_cast<std::size_t>(m.handle)]) {
            std::vector<nav::Vec3> points;points.reserve(local.points.size());
            for(const auto p:local.points)points.push_back(origin+zombies::rotate(p,angles));
            const auto normal=zombies::rotate(local.normal,angles);
            const auto kind=local.kind==Kind::clip?Kind::clip:orientation(normal);
            out.push_back(local_face(points,normal,kind,local.triangle,local.blocking,local.source,local.contents,local.material));
            out.back().moving=true;
        }
        ++posed;
    }
    // Model bounds: SOLID only. Script models without it often report generic cubes
    // (+/-64, +/-128, +/-256 with player clip, live TranZit), not their shape.
    for(const auto number:modelled) {
        std::array<unsigned char,entity_size> e{};
        if(!read(entity_base+number*entity_size,e.data(),e.size())||e[0xFB]!=1||e[0xF9]==1||field<std::uint32_t>(e.data(),0)!=number||
           !(field<std::uint32_t>(e.data(),0x118)&1u))continue;
        const auto origin=field<nav::Vec3>(e.data(),0x134),angles=field<nav::Vec3>(e.data(),0x140);
        const auto lo=field<nav::Vec3>(e.data(),0x100),hi=field<nav::Vec3>(e.data(),0x10C);
        if(!nav::finite(origin)||!nav::finite(angles)||!triggers::valid_bounds(lo,hi)||hi.x-lo.x>2048||hi.y-lo.y>2048||hi.z-lo.z>2048)continue;
        const auto d=origin-eye;
        if(d.x*d.x+d.y*d.y>(radius+2048)*(radius+2048))continue;
        triggers::Mesh mesh;if(!triggers::box_mesh(lo,hi,mesh))continue;
        for(const auto& face:mesh.faces) {
            std::vector<nav::Vec3> points;points.reserve(face.size());
            for(const auto p:face)points.push_back(origin+zombies::rotate(p,angles));
            const auto n=triggers::cross(points[1]-points[0],points[2]-points[0]);const float l=std::sqrt(nav::dot(n,n));
            if(l<1e-6f)continue;
            out.push_back(local_face(points,n*(1/l),orientation(n*(1/l)),false,true,number,field<std::uint32_t>(e.data(),0x118),-1));
            out.back().moving=true;out.back().bounds=true;
        }
        ++posed;
    }
    return posed;
}
void clear() {std::lock_guard lock(guard);current.reset();built_roots={};movers_found.clear();model_entities.clear();}
std::shared_ptr<const Snapshot> snapshot() {std::lock_guard lock(guard);return current;}
const char* status() {std::lock_guard lock(guard);return state;}
}
