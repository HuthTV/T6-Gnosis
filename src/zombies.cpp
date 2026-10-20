#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <mutex>
#include "memory.hpp"
#include "zombies.hpp"
#include "triggers.hpp"
#include "trigger_geometry_fingerprints.hpp"
#include "zombie_fingerprints.hpp"
#include "despawn.hpp"

namespace zombies {
namespace {
std::mutex guard;
std::shared_ptr<const Snapshot> current;
bool ready=false;
const char* state="Zombie bindings disabled";
constexpr std::uint32_t entity_pointer=0x2338784,entity_count=0x233878C,actor_pointer=0x23387AC;
constexpr std::uint32_t entity_base=0x21C13C0;
bool read(std::uint32_t a,void* out,std::size_t size) {
    if(a<0x10000||size>0x400000||size>0xFFFFFFFFu-a)return false;
    return memory::read(a,out,size);
}
template<class T> bool read(std::uint32_t a,T& out) {return read(a,&out,sizeof(out));}
bool string(std::uint32_t table,std::uint16_t token,char* out,std::size_t size) {
    return token&&read(table+static_cast<std::uint32_t>(token)*24+4,out,size)&&std::memchr(out,0,size);
}
bool layout(std::uint32_t& root,std::uint32_t& count) {
    std::uint32_t base=0;return read(entity_pointer,base)&&base==entity_base&&read(entity_count,count)&&count>0&&count<=1024&&read(actor_pointer,root)&&root>=0x10000&&root<=0xFFFFFFFFu-32*0x2720;
}
void publish_failure(const char* reason) {std::lock_guard lock(guard);current.reset();state=reason;}
}
bool startup() {
    ready=true;
    for(const auto& f:zombie_fingerprints) {
        std::vector<unsigned char> raw(f.size);
        if(!read(f.address,raw.data(),raw.size())||std::memcmp(raw.data(),f.bytes,f.size)) {ready=false;break;}
    }
    if(!ready)publish_failure("Zombie binding mismatch; node viewer remains available");
    else publish_failure("Waiting for living zombies");
    return ready;
}
void clear() {publish_failure("No active zombie snapshot");}
std::shared_ptr<const Snapshot> snapshot() {std::lock_guard lock(guard);return current;}
const char* status() {std::lock_guard lock(guard);return state;}
bool still_living(const Marker& m,nav::Vec3& origin) {
    std::array<unsigned char,0x31C> entity{};std::uint32_t root=0,count=0,backlink=0,active=0;
    if(!ready||!layout(root,count)||m.number>=count||!actor_address(root,m.actor)||m.entity!=entity_base+m.number*0x31Cu||!read(m.entity,entity))return false;
    if(!living_entity(entity.data(),m.number,m.actor,m.sentient)||field<std::uint16_t>(entity.data(),0x176)!=m.classname||
        !read(m.actor+4,backlink)||backlink!=m.entity||!read(m.actor+0x1BC8,active)||active==0)return false;
    origin=field<nav::Vec3>(entity.data(),0x134);return nav::finite(origin);
}
bool living_bounds(const Marker& m,nav::Vec3& origin,nav::Vec3& mins,nav::Vec3& maxs) {
    std::array<unsigned char,0x31C> entity{};std::uint32_t root=0,count=0,backlink=0,active=0;
    if(!ready||!layout(root,count)||m.number>=count||!actor_address(root,m.actor)||m.entity!=entity_base+m.number*0x31Cu||!read(m.entity,entity))return false;
    if(!living_entity(entity.data(),m.number,m.actor,m.sentient)||field<std::uint16_t>(entity.data(),0x176)!=m.classname||
        !read(m.actor+4,backlink)||backlink!=m.entity||!read(m.actor+0x1BC8,active)||active==0)return false;
    // Same entity bounds fields the player box reads (+0x100 mins, +0x10C maxs).
    origin=field<nav::Vec3>(entity.data(),0x134);mins=field<nav::Vec3>(entity.data(),0x100);maxs=field<nav::Vec3>(entity.data(),0x10C);
    return nav::finite(origin)&&nav::finite(mins)&&nav::finite(maxs)&&mins.x<maxs.x&&mins.y<maxs.y&&mins.z<maxs.z&&
        maxs.x-mins.x<=256&&maxs.y-mins.y<=256&&maxs.z-mins.z<=256;
}
void update() {
    if(!ready)return;
    auto g=nav::graph();nav::Camera camera{};
    if(!g||!nav::read_camera(camera,*g)) {clear();return;}
    std::uint32_t root=0,count=0,table=0;
    if(!layout(root,count)||!read(0x2BF2DA4,table)||!table) {publish_failure("Invalid entity/actor layout");return;}
    std::vector<unsigned char> entities(count*0x31Cu),actors(32*0x2720);
    if(!read(entity_base,entities.data(),entities.size())||!read(root,actors.data(),actors.size())) {clear();return;}
    auto next=std::make_shared<Snapshot>();next->map=g;next->updated=GetTickCount64();
    const auto identities=despawn::snapshot();
    const bool identities_valid=identities&&despawn::fresh(*identities,*g,next->updated);
    for(unsigned slot=0;slot<32;++slot) {
        const auto* a=actors.data()+slot*0x2720;
        if(!(a[0]&1)||field<std::uint32_t>(a,0x1BC8)==0||a[0x1DB]!=0)continue;
        const auto entity=field<std::uint32_t>(a,4),sentient=field<std::uint32_t>(a,8),actor=root+slot*0x2720;
        if(entity<entity_base||(entity-entity_base)%0x31C||!sentient)continue;
        const auto id=(entity-entity_base)/0x31C;if(id>=count)continue;
        const auto* e=entities.data()+id*0x31C;
        if(!living_entity(e,id,actor,sentient))continue;
        std::array<std::uint32_t,2> s{};
        if(!read(sentient,s)||s[0]!=entity||s[1]!=2)continue;
        const auto classname=field<std::uint16_t>(e,0x176);char name[96]{};
        const int special=despawn::special(identities_valid?identities.get():nullptr,static_cast<int>(id));
        if(!string(table,classname,name,sizeof(name))||(!zombie_class(name)&&!special))continue;
        const auto origin=field<nav::Vec3>(e,0x134);
        if(!nav::finite(origin))continue;
        Marker marker{};marker.entity=entity;marker.actor=actor;marker.sentient=sentient;marker.number=static_cast<std::uint16_t>(id);marker.classname=classname;
        marker.special=special;
        nav::Vec3 verify{};if(still_living(marker,verify))next->markers.push_back(marker);
    }
    std::uint32_t verify_root=0,verify_count=0;
    if(!layout(verify_root,verify_count)||verify_root!=root||verify_count!=count||!nav::read_camera(camera,*g)) {clear();return;}
    // Despawns: a zombie living last frame whose entity slot is now free, or holds
    // another class, was deleted while alive. A killed zombie keeps its entity, with
    // health 0, through its death animation, so kills are not counted. Read once per
    // game frame, so a despawn is reported at most a frame late.
    static std::vector<Marker> previous;static std::shared_ptr<const nav::Graph> previous_map;
    if(previous_map&&spawns::same_map(*previous_map,*g))for(const auto& old:previous) {
        bool present=false;
        for(const auto& marker:next->markers)if(marker.actor==old.actor&&marker.number==old.number) {present=true;break;}
        if(present||old.number>=count)continue;
        const auto* e=entities.data()+old.number*0x31Cu;
        if(e[0xFB]!=1||field<std::uint16_t>(e,0x176)!=old.classname)despawn::zombie_deleted(g,old.number);
    }
    previous=next->markers;previous_map=g;
    std::lock_guard lock(guard);current=std::move(next);state="Living zombie markers ready";
}
}

// Shares the existing fingerprint-checked entity layout, not an embedded map list.
namespace triggers {
namespace {
std::mutex guard;std::shared_ptr<const Snapshot> current;
struct Metadata {std::shared_ptr<const nav::Graph> map;std::uint16_t classname=0;std::uint32_t use_count=0,birthtime=0;std::uint64_t updated=0;std::vector<ScriptField> fields;};
std::array<Metadata,1024> descriptions;
}
void metadata(int entity,const char* classname,const char* key,const char* value) {
 if(entity<0||entity>=1024||!classname||!candidate(classname)||!key||!value||(!metadata_key(key)&&key[0]))return;
 auto map=nav::graph();nav::Camera camera{};std::uint32_t root=0,count=0,table=0;
 std::array<unsigned char,0x31C> e{};char live_name[96]{};
 if(!zombies::ready||!map||!nav::read_camera(camera,*map)||!zombies::layout(root,count)||static_cast<unsigned>(entity)>=count||
 !zombies::read(zombies::entity_base+static_cast<unsigned>(entity)*0x31Cu,e)||e[0xFB]!=1||zombies::field<std::uint32_t>(e.data(),0)!=static_cast<unsigned>(entity)||
 !zombies::read(0x2BF2DA4,table)||!zombies::string(table,zombies::field<std::uint16_t>(e.data(),0x176),live_name,sizeof(live_name))||std::strcmp(classname,live_name))return;
 const auto token=zombies::field<std::uint16_t>(e.data(),0x176);const auto use=zombies::field<std::uint32_t>(e.data(),0x304),birth=zombies::field<std::uint32_t>(e.data(),0x310);
 std::lock_guard lock(guard);auto& d=descriptions[static_cast<unsigned>(entity)];
 if(!key[0]){d={map,token,use,birth,GetTickCount64(),{}};return;}
 if(!d.map||d.map->map!=map->map||d.map->data!=map->data||d.map->base!=map->base||d.classname!=token||d.use_count!=use||d.birthtime!=birth||d.fields.size()>=12)return;
 for(auto& f:d.fields)if(f.key==key){f.value=value;return;}
 if(value[0])d.fields.push_back({key,value});
}
void clear() {std::lock_guard lock(guard);current.reset();descriptions={};}
std::shared_ptr<const Snapshot> snapshot() {std::lock_guard lock(guard);return current;}
bool player_box(PlayerBox& out) {
 std::uint32_t root=0,count=0,table=0;if(!zombies::ready||!zombies::layout(root,count)||!zombies::read(0x2BF2DA4,table)||!table)return false;
 unsigned found=0;PlayerBox latest{};
 for(unsigned id=0;id<std::min(count,18u);++id) {
  std::array<unsigned char,0x31C> e{},verify{};char name[96]{};const auto address=zombies::entity_base+id*0x31Cu;
  if(!zombies::read(address,e)||e[0xFB]!=1||zombies::field<std::uint32_t>(e.data(),0)!=id)continue;
  const auto token=zombies::field<std::uint16_t>(e.data(),0x176);if(!zombies::string(table,token,name,sizeof(name))||std::strcmp(name,"player"))continue;
  const auto client=zombies::field<std::uint32_t>(e.data(),0x154);if(client<0x10000)return false;
  if(!zombies::read(address,verify)||verify[0xFB]!=1||zombies::field<std::uint32_t>(verify.data(),0)!=id||
    zombies::field<std::uint16_t>(verify.data(),0x176)!=token||zombies::field<std::uint32_t>(verify.data(),0x154)!=client||!stable_shape(e.data(),verify.data()))return false;
  latest.origin=zombies::field<nav::Vec3>(verify.data(),0x134);latest.mins=zombies::field<nav::Vec3>(verify.data(),0x100);latest.maxs=zombies::field<nav::Vec3>(verify.data(),0x10C);
  if(!nav::finite(latest.origin)||!valid_bounds(latest.mins,latest.maxs))return false;
  ++found;
 }
 if(found!=1)return false;out=latest;return true;
}
bool current_origin(const Marker& marker,nav::Vec3& origin) {nav::Vec3 angles{};return current_pose(marker,origin,angles);}
bool current_pose(const Marker& marker,nav::Vec3& origin,nav::Vec3& angles) {
 std::uint32_t root=0,count=0,table=0;std::array<unsigned char,0x31C> e{},verify{};
 if(!zombies::ready||!zombies::layout(root,count)||marker.number>=count||!zombies::read(0x2BF2DA4,table)||!table)return false;
 const auto address=zombies::entity_base+marker.number*0x31Cu;
 if(!zombies::read(address,e)||e[0xFB]!=1||zombies::field<std::uint32_t>(e.data(),0)!=marker.number||zombies::field<std::uint16_t>(e.data(),0x176)!=marker.classname)return false;
 if(e[0xF9]!=marker.bmodel||e[0xFA]!=marker.flags||zombies::field<std::int16_t>(e.data(),0xDC)!=marker.handle||
 zombies::field<std::uint32_t>(e.data(),0x304)!=marker.use_count||zombies::field<std::uint32_t>(e.data(),0x310)!=marker.birthtime||
 std::memcmp(e.data()+0x100,&marker.mins,12)||std::memcmp(e.data()+0x10C,&marker.maxs,12))return false;
 char name[96]{};if(!zombies::string(table,marker.classname,name,sizeof(name))||marker.name!=name||!candidate(name))return false;
 origin=zombies::field<nav::Vec3>(e.data(),0x134);
 if(!nav::finite(origin)||std::abs(origin.x)>1000000||std::abs(origin.y)>1000000||std::abs(origin.z)>1000000)return false;
 if(!zombies::read(address,verify)||verify[0xFB]!=1||zombies::field<std::uint32_t>(verify.data(),0)!=marker.number||zombies::field<std::uint16_t>(verify.data(),0x176)!=marker.classname)return false;
 // Motion is allowed. Only shape/identity fields must stay stable between reads.
 if(!stable_shape(e.data(),verify.data()))return false;
 origin=zombies::field<nav::Vec3>(verify.data(),0x134);angles=zombies::field<nav::Vec3>(verify.data(),0x140);
 return nav::finite(origin)&&nav::finite(angles)&&std::abs(origin.x)<=1000000&&std::abs(origin.y)<=1000000&&std::abs(origin.z)<=1000000;
}
void update() {
 const auto map=nav::graph();nav::Camera camera{};std::uint32_t root=0,count=0,table=0;
 if(!zombies::ready||!available()||!map||!nav::read_camera(camera,*map)||!zombies::layout(root,count)||!zombies::read(0x2BF2DA4,table)||!table) {clear();return;}
 std::vector<unsigned char> entities(count*0x31Cu);
 if(!zombies::read(zombies::entity_base,entities.data(),entities.size())) {clear();return;}
 auto next=std::make_shared<Snapshot>();next->map=map;next->updated=GetTickCount64();
 for(std::uint32_t id=0;id<count;++id) {
  const auto* e=entities.data()+id*0x31C;if(e[0xFB]!=1||zombies::field<std::uint32_t>(e,0)!=id)continue;
  const auto token=zombies::field<std::uint16_t>(e,0x176);char name[96]{};
  if(!zombies::string(table,token,name,sizeof(name))||!candidate(name))continue;
  Marker marker{};marker.number=static_cast<std::uint16_t>(id);marker.classname=token;marker.name=name;
  marker.use_count=zombies::field<std::uint32_t>(e,0x304);marker.birthtime=zombies::field<std::uint32_t>(e,0x310);
  if(!read_geometry(e,*map,marker)){++next->unsupported;continue;}
  nav::Vec3 origin{};if(!current_origin(marker,origin))continue;
  {
   std::lock_guard lock(guard);const auto& d=descriptions[id];
   if(d.map&&d.map->map==map->map&&d.map->data==map->data&&d.map->base==map->base&&d.classname==marker.classname&&d.use_count==marker.use_count&&d.birthtime==marker.birthtime&&next->updated>=d.updated&&next->updated-d.updated<=2500)marker.fields=d.fields;
  }
  next->markers.push_back(std::move(marker));
 }
 std::uint32_t vr=0,vc=0,vt=0;
 if(!zombies::layout(vr,vc)||vr!=root||vc!=count||!zombies::read(0x2BF2DA4,vt)||vt!=table||!nav::read_camera(camera,*map)) {clear();return;}
 std::lock_guard lock(guard);current=std::move(next);
}
}
// Global collision-model path verified in the current T6 executable.
namespace triggers {
namespace {
bool geometry_ready=false;
struct CollisionRoots {
 std::uint32_t name=0,model_count=0,models=0,node_count=0,nodes=0,leaf_count=0,leafs=0,side_count=0,sides=0,plane_count=0,planes=0,brushes=0;
 std::uint16_t brush_count=0;
 bool operator==(const CollisionRoots& b) const {return name==b.name&&model_count==b.model_count&&models==b.models&&node_count==b.node_count&&nodes==b.nodes&&leaf_count==b.leaf_count&&leafs==b.leafs&&side_count==b.side_count&&sides==b.sides&&plane_count==b.plane_count&&planes==b.planes&&brushes==b.brushes&&brush_count==b.brush_count;}
};
bool collision_roots(CollisionRoots& r,const nav::Graph& map) {
 char name[160]{};
 if(!zombies::read(0x2503880,r.name)||!zombies::read(r.name,name,sizeof(name))||!std::memchr(name,0,sizeof(name))||map.map!=name)return false;
 return zombies::read(0x2503910,r.model_count)&&r.model_count>0&&r.model_count<=16384&&zombies::read(0x2503914,r.models)&&r.models>=0x10000&&
 zombies::read(0x25038A0,r.node_count)&&r.node_count>0&&r.node_count<=200000&&zombies::read(0x25038A4,r.nodes)&&r.nodes>=0x10000&&
 zombies::read(0x25038A8,r.leaf_count)&&r.leaf_count>0&&r.leaf_count<=1000000&&zombies::read(0x25038AC,r.leafs)&&r.leafs>=0x10000&&
 zombies::read(0x2503898,r.side_count)&&r.side_count<=1000000&&zombies::read(0x250389C,r.sides)&&
 zombies::read(0x2503888,r.plane_count)&&r.plane_count>0&&r.plane_count<=1000000&&zombies::read(0x250388C,r.planes)&&r.planes>=0x10000&&
 zombies::read(0x25038C0,r.brush_count)&&r.brush_count>0&&zombies::read(0x25038C4,r.brushes)&&r.brushes>=0x10000&&
 static_cast<std::uint64_t>(r.models)+r.model_count*76ull<=0x100000000ull&&static_cast<std::uint64_t>(r.nodes)+r.node_count*20ull<=0x100000000ull&&
 static_cast<std::uint64_t>(r.brushes)+r.brush_count*96ull<=0x100000000ull;
}
bool contained(std::uint32_t address,std::uint32_t base,std::uint32_t count,std::uint32_t stride,std::uint32_t length=1) {
 return base>=0x10000&&address>=base&&(address-base)%stride==0&&length<=count&&
 (address-base)/stride<=count-length&&static_cast<std::uint64_t>(base)+static_cast<std::uint64_t>(count)*stride<=0x100000000ull;
}
bool leaf_brushes(const CollisionRoots& r,std::uint32_t index,unsigned depth,std::vector<std::uint32_t>& visited,std::vector<std::uint16_t>& brushes) {
 if(index>=r.node_count||depth>32||visited.size()>=512||std::find(visited.begin(),visited.end(),index)!=visited.end())return false;
 visited.push_back(index);std::array<unsigned char,20> node{};
 if(!zombies::read(r.nodes+index*20,node))return false;
 const auto count=zombies::field<std::int16_t>(node.data(),2);
 if(count>0) {
  if(count>64)return false;const auto list=zombies::field<std::uint32_t>(node.data(),8);
  // cLeafBrushNode lists can be independently allocated; they are not required
  // to be subranges of ClipInfo.leafbrushes. Bound both list and brush indices,
  // and verify the owning node and copied list before accepting them.
  if(!brush_list_address(list,static_cast<std::uint32_t>(count)))return false;
  std::vector<std::uint16_t> ids(static_cast<std::size_t>(count)),verify_ids(ids.size());std::array<unsigned char,20> verify_node{};
  if(!zombies::read(list,ids.data(),ids.size()*2)||!zombies::read(r.nodes+index*20,verify_node)||verify_node!=node||
     !zombies::read(list,verify_ids.data(),verify_ids.size()*2)||ids!=verify_ids)return false;
  for(const auto id:ids){if(id>=r.brush_count)return false;if(std::find(brushes.begin(),brushes.end(),id)==brushes.end())brushes.push_back(id);}
  return brushes.size()<=32;
 }
 if(count<0&&!leaf_brushes(r,index+1,depth+1,visited,brushes))return false;
 for(unsigned side=0;side<2;++side){const auto child=zombies::field<std::uint16_t>(node.data(),16+side*2);if(!child||!leaf_brushes(r,index+child,depth+1,visited,brushes))return false;}
 return true;
}
bool brush_meshes(std::int16_t handle,const nav::Graph& map,std::vector<Mesh>& output) {
 CollisionRoots roots{},after{};
 if(!collision_roots(roots,map)||handle<=0||static_cast<std::uint32_t>(handle)>=roots.model_count)return false;
 std::array<unsigned char,0x4C> model{},verify_model{};
 const auto address=roots.models+static_cast<std::uint32_t>(handle)*0x4Cu;
 if(!zombies::read(address,model))return false;
 const auto info=zombies::field<std::uint32_t>(model.data(),28);if(info&&info!=0x2503888)return false;
 const auto node=zombies::field<std::int32_t>(model.data(),0x44);if(node<0)return false;
 std::vector<std::uint32_t> visited;std::vector<std::uint16_t> ids;
 if(!leaf_brushes(roots,static_cast<std::uint32_t>(node),0,visited,ids)||ids.empty())return false;
 for(const auto id:ids) {
  std::array<unsigned char,96> brush{},verify{};if(!zombies::read(roots.brushes+id*96u,brush))return false;
  const auto lo=zombies::field<nav::Vec3>(brush.data(),0),hi=zombies::field<nav::Vec3>(brush.data(),16);
  const auto side_count=zombies::field<std::uint32_t>(brush.data(),28),sides=zombies::field<std::uint32_t>(brush.data(),32);
  if(!valid_bounds(lo,hi)||side_count>18||(side_count&&!contained(sides,roots.sides,roots.side_count,12,side_count)))return false;
  auto planes=axial_planes(lo,hi);
  for(std::uint32_t side=0;side<side_count;++side) {
   std::uint32_t pointer=0;std::array<float,4> plane{};
   if(!zombies::read(sides+side*12,pointer)||!contained(pointer,roots.planes,roots.plane_count,20)||!zombies::read(pointer,plane))return false;
   planes.push_back({{plane[0],plane[1],plane[2]},plane[3]});
  }
  Mesh mesh;if(!convex_mesh(planes,mesh)||!zombies::read(roots.brushes+id*96u,verify)||brush!=verify)return false;
  output.push_back(std::move(mesh));
 }
 return collision_roots(after,map)&&after==roots&&zombies::read(address,verify_model)&&verify_model==model;
}
}
bool available() {return geometry_ready;}
bool startup() {
 geometry_ready=zombies::ready;
 for(const auto& f:trigger_geometry_fingerprints) {
  std::vector<unsigned char> bytes(f.size);
  if(!zombies::read(f.address,bytes.data(),bytes.size())||std::memcmp(bytes.data(),f.bytes,f.size)){geometry_ready=false;break;}
 }
 return geometry_ready;
}
bool read_geometry(const unsigned char* e,const nav::Graph& map,Marker& marker) {
 marker.mins=zombies::field<nav::Vec3>(e,0x100);marker.maxs=zombies::field<nav::Vec3>(e,0x10C);marker.angles=zombies::field<nav::Vec3>(e,0x140);
 marker.bmodel=e[0xF9];marker.flags=e[0xFA];marker.handle=zombies::field<std::int16_t>(e,0xDC);
 if(!nav::finite(marker.angles)||!valid_bounds(marker.mins,marker.maxs))return false;
 if(marker.name=="trigger_box"&&marker.bmodel==0&&marker.flags==1) {
  Mesh mesh;if(!box_mesh(marker.mins,marker.maxs,mesh))return false;marker.meshes.push_back(std::move(mesh));marker.shape="Box";return true;
 }
 if((marker.name=="trigger_radius"||marker.name=="trigger_radius_use")&&marker.bmodel==0&&marker.flags==0x21&&radial_bounds(marker.mins,marker.maxs)) {
  // Native radial contact ignores orientation: cylinder stays world vertical.
  Mesh mesh;if(!cylinder_mesh(marker.maxs.x,marker.maxs.z,mesh))return false;marker.meshes.push_back(std::move(mesh));marker.shape="Cylinder";return true;
 }
 if((marker.name=="trigger_use"||marker.name=="info_volume")&&marker.bmodel==1&&(marker.flags&0x60)==0) {
  if(!brush_meshes(marker.handle,map,marker.meshes))return false;marker.shape="Brush";return true;
 }
 return false;
}
}

