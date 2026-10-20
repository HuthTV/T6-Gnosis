#pragma once
#include "spawns_batch.hpp"
#include <array>
#include <cstring>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace spawns::native {
enum class State {ready,busy,invalid};
struct Tables {std::vector<unsigned char> objects,children,values;};
using Read=std::function<bool(std::uint32_t,void*,std::size_t)>;
using Name=std::function<bool(std::uint32_t,std::string&)>;
struct VectorRead {std::uint32_t address;nav::Vec3 value;};
struct Decoded {State state=State::invalid;const char* reason="Invalid native spawn data";std::vector<Site> sites;unsigned active_count=0;std::uint32_t script_token=0;std::array<std::vector<std::uint32_t>,3> pages;std::vector<VectorRead> vectors;};
struct Value {std::uint32_t payload=0,key=0;unsigned char type=0;};
class Decoder {
    const Tables& t;
    const Read& read;
    const Name& canonical;
    const Name& ordinary;
    const std::uint32_t canonical_limit;
    unsigned objects=0,children=0,inputs=0;
    std::unordered_map<std::uint32_t,std::vector<Value>> cache;
    std::array<std::unordered_set<std::uint32_t>,3> pages;
    void page(unsigned table,std::uint32_t offset,unsigned size) {pages[table].insert(offset/4096);pages[table].insert((offset+size-1)/4096);}
    template<class T> static T field(const unsigned char* p,std::size_t offset) {T v{};std::memcpy(&v,p+offset,sizeof(v));return v;}
    bool object(std::uint32_t id,unsigned& type,unsigned& size) const {
        if(!id||id>=32768||static_cast<std::size_t>(id)*8+8>t.objects.size())return false;
        const auto* p=t.objects.data()+id*8;type=field<std::uint32_t>(p,4)&0x7F;size=field<std::uint16_t>(p,2);
        return type==18||type==21;
    }
    bool walk(std::uint32_t id,unsigned wanted,const std::vector<Value>*& out) {
        unsigned type=0,size=0;if(!object(id,type,size)||type!=wanted)return false;
        const auto found=cache.find(id);if(found!=cache.end()) {out=&found->second;return true;}
        if(++objects>10000||static_cast<std::size_t>(id)*8+8>t.children.size())return false;
        const auto* links=t.children.data()+id*8;
        page(0,id*8,8);page(1,id*8,8);
        std::uint32_t current=field<std::uint32_t>(links,0),last=field<std::uint32_t>(links,4),previous=0;
        std::unordered_set<std::uint32_t> seen;std::vector<Value> result;
        while(current) {
            if(current>=262144||static_cast<std::size_t>(current)*28+28>t.values.size()||!seen.insert(current).second||result.size()>=8192||++children>24000)return false;
            const auto* p=t.values.data()+current*28;
            page(2,current*28,28);
            if(field<std::uint16_t>(p,18)!=id||field<std::uint32_t>(p,24)!=previous||p[12]==0||p[12]==23)return false;
            result.push_back({field<std::uint32_t>(p,0),static_cast<std::uint32_t>(p[13])+(static_cast<std::uint32_t>(field<std::uint16_t>(p,16))<<8),p[12]});
            previous=current;current=field<std::uint32_t>(p,20);
        }
        if(previous!=last||(type==21&&result.size()!=size))return false;
        out=&cache.emplace(id,std::move(result)).first->second;return true;
    }
    bool fields(std::uint32_t id,std::unordered_map<std::string,Value>& out) {
        const std::vector<Value>* values=nullptr;if(!walk(id,18,values))return false;
        for(const auto& value:*values) {
            // A live level can also contain non-canonical child keys (observed
            // 0x18000). They are not names in the relocated script-field table.
            // walk still validates their ownership and sibling links.
            if(value.key>=canonical_limit)continue;
            std::string name;if(!canonical(value.key,name)||name.empty()||!out.emplace(name,value).second)return false;
        }
        return true;
    }
    bool array(Value value,const std::vector<Value>*& out) {return value.type==1&&walk(value.payload,21,out);}
    bool integer(Value value,int& out) const {if(value.type!=7)return false;std::memcpy(&out,&value.payload,4);return true;}
public:
    Decoder(const Tables& tables,const Read& reader,const Name& names,const Name& strings,std::uint32_t name_limit=65536):t(tables),read(reader),canonical(names),ordinary(strings),canonical_limit(name_limit) {}
    Decoded decode(std::uint32_t level,const nav::Graph& graph) {
        Decoded result;
        std::unordered_map<std::string,Value> root;
        if(!fields(level,root)) {result.reason="Native level structure invalid";return result;}
        const auto script=root.find("script");std::string map;
        if(script==root.end()||script->second.type!=2||!ordinary(script->second.payload,map)||!map_name_matches(graph,map)) {result.reason="Native spawn map mismatch";return result;}
        const auto scanning=root.find("zone_scanning_active");int scan=0;
        if(scanning!=root.end()&&(!integer(scanning->second,scan)||(scan!=0&&scan!=1))) {result.reason="Native zone scan flag invalid";return result;}
        if(scan) {result.state=State::busy;result.reason="Zone scan in progress";return result;}
        const auto flags=root.find("flag");const std::vector<Value>* flag_values=nullptr;bool initialized=false;
        if(flags==root.end()) {result.state=State::busy;result.reason="Waiting for initialized zones";return result;}
        if(!array(flags->second,flag_values)) {result.reason="Native zone readiness registry invalid";return result;}
        for(const auto& value:*flag_values) {
            if(value.key>=0x40000)continue;
            std::string name;if(!ordinary(value.key,name)) {result.reason="Native flag key invalid";return result;}
            if(name=="zones_initialized") {int enabled=0;if(!integer(value,enabled)||(enabled!=0&&enabled!=1)) {result.reason="Native zone readiness flag invalid";return result;}initialized=enabled==1;break;}
        }
        if(!initialized) {result.state=State::busy;result.reason="Waiting for initialized zones";return result;}
        const auto zones=root.find("zones"),pool=root.find("zombie_spawn_locations");
        const std::vector<Value>* zone_values=nullptr,*pool_values=nullptr;
        if(zones==root.end()||pool==root.end()) {result.state=State::busy;result.reason="Waiting for spawn registry";return result;}
        if(!array(zones->second,zone_values)||zone_values->size()>256||!array(pool->second,pool_values)||pool_values->size()>4096) {result.reason="Native spawn registry bounds invalid";return result;}
        std::unordered_set<std::uint32_t> active;std::unordered_map<std::uint32_t,nav::Vec3> sites;
        std::unordered_map<std::uint32_t,unsigned> types;
        const auto observe=[&](Value value,bool is_active,bool needs_enabled=true)->bool {
            if(++inputs>8192||value.type!=1)return false;
            std::unordered_map<std::string,Value> site;
            if(!fields(value.payload,site))return false;
            const auto origin=site.find("origin");nav::Vec3 position{};
            if(origin==site.end()||origin->second.type!=4||!read(origin->second.payload,&position,sizeof(position))||!valid_origin(position))return false;
            const auto enabled=site.find("is_enabled");int state=0;
            if(needs_enabled&&(enabled==site.end()||!integer(enabled->second,state)||(state!=0&&state!=1)))return false;
            // Membership in the actual chooser pool determines eligibility.
            // Custom spawn checks may override the ordinary enabled field.
            const auto existing=sites.find(value.payload);
            if(existing!=sites.end()&&std::memcmp(&existing->second,&position,sizeof(position)))return false;
            if(sites.emplace(value.payload,position).second)result.vectors.push_back({origin->second.payload,position});
            if(sites.size()>4096)return false;
            if(is_active)active.insert(value.payload);
            return true;
        };
        for(const auto& zone:*zone_values) {
            if(zone.type!=1) {result.reason="Native zone reference invalid";return result;}
            std::unordered_map<std::string,Value> fields_in_zone;
            if(!fields(zone.payload,fields_in_zone)) {result.reason="Native zone structure invalid";return result;}
            for(const auto& registry:{std::pair<const char*,unsigned>{"spawn_locations",1}, {"leaper_locations",2}, {"screecher_locations",4}, {"mechz_locations",16}, {"brutus_locations",64}}) {
                if(registry.second==2&&map!="zm_highrise")continue;
                if(registry.second==4&&map!="zm_transit")continue;
                if(registry.second==16&&map!="zm_tomb")continue;
                if(registry.second==64&&map!="zm_prison")continue;
                const auto locations=fields_in_zone.find(registry.first);if(locations==fields_in_zone.end())continue;
                const std::vector<Value>* values=nullptr;
                if(!array(locations->second,values)) {result.reason="Native zone locations invalid";return result;}
                for(const auto& value:*values) {
                    if(!observe(value,false)) {result.reason="Native configured site invalid";return result;}
                    types[value.payload]|=registry.second;
                }
            }
        }
        for(const auto& value:*pool_values) {
            if(!observe(value,true)) {result.reason="Native active spawn site invalid";return result;}
            types[value.payload]|=1;
        }
        // Mob's complete authored registry is independent of its current chooser pool.
        if(map=="zm_prison") {
            const auto registry=root.find("brutus_spawn_positions");
            if(registry!=root.end()) {
                const std::vector<Value>* values=nullptr;
                if(!array(registry->second,values)) {result.reason="Native Brutus registry invalid";return result;}
                for(const auto& value:*values) {
                    if(!observe(value,false,false)) {result.reason="Native Brutus point invalid";return result;}
                    types[value.payload]|=64;
                }
            }
        }
        // Buried ghost points are authored structs, not normal enabled spawn sites.
        // Read registries only; never call the randomized/tracing spawn chooser.
        if(map=="zm_buried") {
            const auto ghost_locations=[&](Value registry)->bool {
                const std::vector<Value>* locations=nullptr;
                if(!array(registry,locations)||locations->size()>4096)return false;
                for(const auto& value:*locations) {
                    if(!observe(value,false,false))return false;
                    types[value.payload]|=8;
                }
                return true;
            };
            const auto rooms=root.find("ghost_rooms");
            if(rooms!=root.end()) {
                const std::vector<Value>* room_values=nullptr;
                if(!array(rooms->second,room_values)||room_values->size()>256) {result.reason="Ghost room registry invalid";return result;}
                for(const auto& room:*room_values) {
                    std::unordered_map<std::string,Value> room_fields;
                    if(room.type!=1||!fields(room.payload,room_fields)) {result.reason="Ghost room structure invalid";return result;}
                    const auto points=room_fields.find("ghost_spawn_locations");
                    if(points!=room_fields.end()&&!ghost_locations(points->second)) {result.reason="Ghost room spawn locations invalid";return result;}
                }
            }
            for(const auto name:{"ghost_zone_start_lower_locations","ghost_front_standing_locations","ghost_back_standing_locations"}) {
                const auto registry=root.find(name);
                if(registry!=root.end()&&!ghost_locations(registry->second)) {result.reason="Ghost entrance spawn locations invalid";return result;}
            }
        }
        // Generator attack/recapture zombies use authored holes linked by target.
        // Cached arrays can be absent until a generator is activated; resolve the
        // same targetname index used by vanilla getstructarray without calling it.
        if(map=="zm_tomb") {
            const auto named_array=[&](Value registry,const std::string& wanted,Value& found)->bool {
                const std::vector<Value>* values=nullptr;if(!array(registry,values))return false;
                for(const auto& value:*values) {
                    if(value.key>=0x40000)continue;
                    std::string name;if(!ordinary(value.key,name))return false;
                    if(name==wanted) {found=value;return true;}
                }
                return false;
            };
            const auto capture=root.find("zone_capture");
            if(capture!=root.end()) {
                std::unordered_map<std::string,Value> capture_fields;
                if(capture->second.type!=1||!fields(capture->second.payload,capture_fields)) {result.reason="Generator capture registry invalid";return result;}
                const auto generators=capture_fields.find("zones");
                if(generators!=capture_fields.end()) {
                    const std::vector<Value>* values=nullptr;
                    if(!array(generators->second,values)||values->size()>64) {result.reason="Generator registry bounds invalid";return result;}
                    for(const auto& generator:*values) {
                        std::unordered_map<std::string,Value> generator_fields;
                        if(generator.type!=1||!fields(generator.payload,generator_fields)) {result.reason="Generator structure invalid";return result;}
                        Value holes{};const auto cached_holes=generator_fields.find("a_emergence_hole_structs");
                        if(cached_holes!=generator_fields.end())holes=cached_holes->second;
                        else {
                            const auto target=generator_fields.find("target"),classes=root.find("struct_class_names");
                            std::string name;Value targets{};
                            if(target==generator_fields.end()||target->second.type!=2||!ordinary(target->second.payload,name)||classes==root.end()||!named_array(classes->second,"targetname",targets)||!named_array(targets,name,holes)) {result.reason="Generator authored holes unavailable";return result;}
                        }
                        const std::vector<Value>* locations=nullptr;
                        if(!array(holes,locations)||locations->size()>256) {result.reason="Generator hole registry invalid";return result;}
                        for(const auto& value:*locations) {
                            if(!observe(value,false,false)) {result.reason="Templar spawn origin invalid";return result;}
                            types[value.payload]|=32;
                        }
                    }
                }
            }
        }
        std::vector<std::uint32_t> ids;ids.reserve(sites.size());for(const auto& item:sites)ids.push_back(item.first);std::sort(ids.begin(),ids.end());
        result.sites.reserve(ids.size());for(auto id:ids)result.sites.push_back({sites.at(id),id,active.count(id)!=0,types.at(id)});
        result.active_count=static_cast<unsigned>(active.size());result.script_token=script->second.payload;
        for(unsigned table=0;table<3;++table)for(auto p:pages[table])result.pages[table].push_back(p);
        result.state=State::ready;result.reason="Native configured spawn sites ready";return result;
    }
};
}
