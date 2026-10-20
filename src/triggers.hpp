#pragma once
#include "navigation.hpp"
#include "trigger_geometry.hpp"
#include "trigger_catalog.hpp"
#include <cstring>
namespace triggers {
inline bool candidate(const char* name) {return name&&(std::strncmp(name,"trigger_",8)==0||std::strcmp(name,"info_volume")==0);}
struct Marker {
 std::uint16_t number=0,classname=0;std::string name,shape;
 nav::Vec3 mins,maxs,angles;unsigned char bmodel=0,flags=0;
 std::int16_t handle=0;std::uint32_t use_count=0,birthtime=0;
 std::vector<Mesh> meshes;
 std::vector<ScriptField> fields;std::vector<std::string> references,candidates;
};
enum class Region {none,enabled_player,disabled_player,unknown_player,life,kill};
inline Region region(const std::vector<ScriptField>& fields) {
 std::string noteworthy,enabled;
 // Only the physical entity's fields participate in the monitor's queries.
 for(const auto& f:fields){if(f.key=="script_noteworthy")noteworthy=f.value;if(f.key=="zone_enabled")enabled=f.value;}
 if(noteworthy=="life_brush")return Region::life;
 if(noteworthy=="kill_brush")return Region::kill;
 if(noteworthy=="player_volume")return enabled=="1"?Region::enabled_player:enabled=="0"?Region::disabled_player:Region::unknown_player;
 return Region::none;
}
inline bool playable_region(Region r) {return r==Region::enabled_player||r==Region::life||r==Region::kill;}
inline bool base_monitor_safe(bool life,bool kill,bool enabled_player) {return life||(!kill&&enabled_player);}
struct Snapshot {std::shared_ptr<const nav::Graph> map;std::vector<Marker> markers;std::uint64_t updated=0;unsigned unsupported=0;};
inline bool fresh(const Snapshot& s,const nav::Graph& g,std::uint64_t now) {return s.map&&now>=s.updated&&now-s.updated<=1000&&s.map->map==g.map&&s.map->base==g.base&&s.map->data==g.data;}
inline std::string label(const std::string& type,const std::vector<ScriptField>& fields) {
 std::string result=type;
 for(const auto key:{"script_noteworthy","targetname"}) {
  const ScriptField* chosen=nullptr;
  for(const auto& f:fields)if(f.key==key&&!f.value.empty()){chosen=&f;break;}
  if(!chosen)for(const auto& f:fields)if(catalog_key(f.key)==key&&!f.value.empty()){chosen=&f;break;}
  if(chosen)result+="\n"+std::string(key)+"="+chosen->value;
 }
 return result;
}
void metadata(int entity,const char* classname,const char* field,const char* value);
struct PlayerBox {nav::Vec3 origin,mins,maxs;};
bool player_box(PlayerBox& out);
bool startup();
bool available();
void clear();
void update();
std::shared_ptr<const Snapshot> snapshot();
bool current_origin(const Marker& marker,nav::Vec3& origin);
// Compare stable shape/lifetime fields; translation and rotation may advance.
inline bool stable_shape(const unsigned char* a,const unsigned char* b) {
 return !std::memcmp(a+0xF9,b+0xF9,3)&&!std::memcmp(a+0xDC,b+0xDC,2)&&!std::memcmp(a+0x100,b+0x100,24)&&
 !std::memcmp(a+0x304,b+0x304,4)&&!std::memcmp(a+0x310,b+0x310,4);
}
bool current_pose(const Marker& marker,nav::Vec3& origin,nav::Vec3& angles);
bool read_geometry(const unsigned char* e,const nav::Graph& map,Marker& marker);
}