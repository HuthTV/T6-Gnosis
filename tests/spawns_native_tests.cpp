#include "spawns_native_decode.hpp"
#include <cstdio>
#include <limits>

namespace {
using spawns::native::State;
int checks=0,failures=0;
void check(bool good,const char* why) {++checks;if(!good) {++failures;std::printf("FAIL: %s\n",why);}}
struct Fixture {
    spawns::native::Tables tables;
    std::unordered_map<std::uint32_t,std::string> names,strings;
    std::unordered_map<std::uint32_t,nav::Vec3> vectors;
    std::uint32_t next=1;
    nav::Graph graph;
    Fixture() {
        tables.objects.resize(0x40000);tables.children.resize(0x40000);tables.values.resize(0x700000);
        graph.data=0x20000;graph.base=0x30000;graph.count=10;graph.map="maps/mp/zm_highrise.d3dbsp";
        names={{1,"script"},{2,"zones"},{3,"zombie_spawn_locations"},{4,"zone_scanning_active"},{5,"flag"},{6,"spawn_locations"},{7,"origin"},{8,"is_enabled"}};
        strings={{10,"zm_highrise"},{50,"zones_initialized"}};
        for(auto id:{3u,20u,30u,31u})object(id,18);
        for(auto id:{10u,11u,12u,21u})object(id,21);
        child(3,1,2,10);child(3,2,1,10);child(3,3,1,11);child(3,4,7,0);child(3,5,1,12);
        child(12,50,7,1);child(10,0x800000,1,20);child(20,6,1,21);
        child(21,0x800000,1,30);child(21,0x800001,1,31);child(11,0x800000,1,30);
        child(30,7,4,0x10000);child(30,8,7,1);child(31,7,4,0x10020);child(31,8,7,0);
        vectors={{0x10000,{1,2,3}},{0x10020,{4,5,6}}};
    }
    template<class T> void put(std::vector<unsigned char>& data,std::size_t at,T value) {std::memcpy(data.data()+at,&value,sizeof(value));}
    template<class T> T get(const std::vector<unsigned char>& data,std::size_t at) const {T value{};std::memcpy(&value,data.data()+at,sizeof(value));return value;}
    void object(std::uint32_t id,std::uint32_t type) {put(tables.objects,id*8+4,type);}
    std::uint32_t child(std::uint32_t parent,std::uint32_t key,unsigned char type,std::uint32_t payload) {
        const auto id=next++,previous=get<std::uint32_t>(tables.children,parent*8+4),offset=id*28;
        put(tables.values,offset,payload);tables.values[offset+12]=type;tables.values[offset+13]=static_cast<unsigned char>(key&255);
        put(tables.values,offset+16,static_cast<std::uint16_t>(key>>8));put(tables.values,offset+18,static_cast<std::uint16_t>(parent));
        put(tables.values,offset+24,previous);
        if(previous)put(tables.values,previous*28+20,id);else put(tables.children,parent*8,id);
        put(tables.children,parent*8+4,id);
        if((get<std::uint32_t>(tables.objects,parent*8+4)&0x7F)==21)put(tables.objects,parent*8+2,static_cast<std::uint16_t>(get<std::uint16_t>(tables.objects,parent*8+2)+1));
        return id;
    }
    std::uint32_t field(std::uint32_t parent,std::uint32_t key) const {
        for(auto id=get<std::uint32_t>(tables.children,parent*8);id;id=get<std::uint32_t>(tables.values,id*28+20))
            if(tables.values[id*28+13]+(static_cast<std::uint32_t>(get<std::uint16_t>(tables.values,id*28+16))<<8)==key)return id;
        return 0;
    }
    void payload(std::uint32_t parent,std::uint32_t key,std::uint32_t value) {put(tables.values,field(parent,key)*28,value);}
    void tag(std::uint32_t parent,std::uint32_t key,unsigned char value) {tables.values[field(parent,key)*28+12]=value;}
    spawns::native::Decoded decode() {
        const spawns::native::Read read=[&](std::uint32_t address,void* out,std::size_t size) {const auto found=vectors.find(address);if(found==vectors.end()||size!=sizeof(nav::Vec3))return false;std::memcpy(out,&found->second,size);return true;};
        const spawns::native::Name canonical=[&](std::uint32_t id,std::string& out) {const auto found=names.find(id);if(found==names.end())return false;out=found->second;return true;};
        const spawns::native::Name ordinary=[&](std::uint32_t id,std::string& out) {const auto found=strings.find(id);if(found==strings.end())return false;out=found->second;return true;};
        spawns::native::Decoder decoder(tables,read,canonical,ordinary);return decoder.decode(3,graph);
    }
};
}
int main() {
    check(spawns::visible_types(9,false)==1,"Hiding special shared origin preserves normal icon" );
    check(spawns::visible_types(8,false)==0,"Hiding special removes ghost-only origin" );
    check(spawns::visible_types(5,false)==1,"Hiding Denizen retains normal icon" );
    check(spawns::visible_types(2,false)==0&&spawns::visible_types(14,true)==14,"Special toggle handles Leaper and visible special masks" );
    {std::vector<spawns::Site> sites{{{1,2,3},1,false,4},{{1,2,3},2,true,1},{{1.01f,2,3},3,false,1}};auto grouped=spawns::drawing_sites(sites);check(grouped.size()==2&&grouped[0].types==5&&grouped[0].active&&grouped[1].types==1,"exact shared origins merge visual identities without merging nearby sites");check(sites.size()==3&&sites[0].types==4,"raw registry identity and counts remain intact");}
    {Fixture f;f.graph.map="maps/mp/zm_transit.d3dbsp";f.strings[10]="zm_transit";f.names[9]="screecher_locations";f.object(22,21);f.child(20,9,1,22);f.child(22,0x800000,1,31);auto r=f.decode();check(r.state==State::ready&&r.sites[1].types==5,"TranZit Denizen registry membership is distinguished from normal sites");}
    {Fixture f;f.names[9]="leaper_locations";f.object(22,21);f.child(20,9,1,22);f.child(22,0x800000,1,31);auto r=f.decode();check(r.state==State::ready&&r.sites[1].types==3,"site registered for normal and leaper spawning retains both identities");}
    {Fixture f;f.names[9]="leaper_locations";f.object(22,21);f.object(32,18);f.child(20,9,1,22);f.child(22,0x800000,1,32);f.child(32,7,4,0x10040);f.child(32,8,7,1);f.vectors[0x10040]={9,8,7};auto r=f.decode();check(r.state==State::ready&&r.sites.size()==3&&r.sites[2].types==2&&!r.sites[2].active,"leaper-only registry site has no invented normal chooser status");}
    {Fixture f;f.child(3,98304,1,30);auto r=f.decode();check(r.state==State::ready&&r.sites.size()==2,"live non-canonical level child does not discard spawn registry");}
    {Fixture f;auto r=f.decode();check(r.state==State::ready&&r.sites.size()==2&&r.active_count==1&&r.sites[0].id==30&&r.sites[0].active&&!r.sites[1].active,"actual chooser membership colors complete configured registry");check(r.vectors.size()==2&&!r.pages[0].empty()&&!r.pages[1].empty()&&!r.pages[2].empty(),"all origins and visited VM metadata have second-read guards");}
    {Fixture f;f.payload(3,4,1);check(f.decode().state==State::busy,"active scan waits without producing a partial snapshot");}
    {Fixture f;f.payload(12,50,0);check(f.decode().state==State::busy,"uninitialized zones wait");}
    {Fixture f;f.payload(12,50,2);check(f.decode().state==State::invalid,"malformed readiness value rejected");}
    {Fixture f;f.tag(3,5,7);check(f.decode().state==State::invalid,"malformed flag registry rejected");}
    {Fixture f;f.payload(3,4,2);check(f.decode().state==State::invalid,"malformed scan state rejected");}
    {Fixture f;f.strings[10]="zm_nuked";check(f.decode().state==State::invalid,"script map must match navigation map");}
    {Fixture f;f.graph.map="maps/mp/zm_highrise_other.d3dbsp";check(f.decode().state==State::invalid,"full map name checked");}
    {Fixture f;f.object(3,20);check(f.decode().state==State::invalid,"entity level root unsupported");}
    {Fixture f;f.object(30,20);check(f.decode().state==State::invalid,"entity-backed origin safely rejected");}
    {Fixture f;f.payload(21,0x800000,32768);check(f.decode().state==State::invalid,"object capacity enforced");}
    {Fixture f;f.put(f.tables.children,3*8,262144u);check(f.decode().state==State::invalid,"child capacity enforced");}
    {Fixture f;const auto c=f.field(30,7);f.put(f.tables.values,c*28+18,static_cast<std::uint16_t>(31));check(f.decode().state==State::invalid,"wrong child ownership rejected");}
    {Fixture f;const auto c=f.field(30,8);f.put(f.tables.values,c*28+24,0u);check(f.decode().state==State::invalid,"broken sibling backlink rejected");}
    {Fixture f;const auto c=f.field(30,8);f.put(f.tables.values,c*28+20,c);check(f.decode().state==State::invalid,"sibling cycle rejected");}
    {Fixture f;f.put(f.tables.children,30*8+4,0u);check(f.decode().state==State::invalid,"incorrect tail rejected");}
    {Fixture f;f.put(f.tables.objects,21*8+2,static_cast<std::uint16_t>(1));check(f.decode().state==State::invalid,"array declared size must match traversal");}
    {Fixture f;f.tag(30,7,23);check(f.decode().state==State::invalid,"removed child record rejected");}
    {Fixture f;f.tag(30,7,0);check(f.decode().state==State::invalid,"undefined child record rejected");}
    {Fixture f;f.tag(30,7,6);check(f.decode().state==State::invalid,"origin requires direct vector type");}
    {Fixture f;f.payload(30,7,0x99999);check(f.decode().state==State::invalid,"unreadable vector rejects entire capture");}
    {Fixture f;f.vectors[0x10000].x=std::numeric_limits<float>::quiet_NaN();check(f.decode().state==State::invalid,"NaN vector rejected");}
    {Fixture f;f.vectors[0x10000].y=std::numeric_limits<float>::infinity();check(f.decode().state==State::invalid,"infinite vector rejected");}
    {Fixture f;f.vectors[0x10000].z=1000001;check(f.decode().state==State::invalid,"coordinate bounds enforced");}
    {Fixture f;f.vectors[0x10000]={-1000000,1000000,0};check(f.decode().state==State::ready,"finite coordinate boundary accepted");}
    {Fixture f;f.payload(30,8,2);check(f.decode().state==State::invalid,"invalid enabled flag rejected");}
    {Fixture f;f.payload(30,8,0);auto r=f.decode();check(r.state==State::ready&&r.active_count==1,"custom chooser pool membership remains authoritative");}
    {Fixture f;f.child(21,0x800002,1,30);f.child(11,0x800001,1,30);auto r=f.decode();check(r.state==State::ready&&r.sites.size()==2&&r.active_count==1,"duplicate object references deduplicate without merging coordinates");}
    {Fixture f;f.vectors[0x10020]={1,2,3};auto r=f.decode();check(r.state==State::ready&&r.sites.size()==2,"overlapping sites retain distinct object identities");}
    {Fixture f;f.child(3,1,2,10);check(f.decode().state==State::invalid,"duplicate canonical field rejected");}
    {Fixture f;f.tag(11,0x800000,7);check(f.decode().state==State::invalid,"non-object pool entry rejected");}
    {Fixture f;for(unsigned i=1;i<257;++i)f.child(10,0x800000+i,1,20);check(f.decode().state==State::invalid,"zone count limit enforced");}
    {Fixture f;for(unsigned i=1;i<4097;++i)f.child(11,0x800000+i,1,30);check(f.decode().state==State::invalid,"pool count limit enforced");}
    {Fixture f;for(unsigned i=1;i<256;++i)f.child(10,0x800000+i,1,20);for(unsigned i=2;i<33;++i)f.child(21,0x800000+i,1,30);check(f.decode().state==State::invalid,"repeated input references cannot exceed bounded traversal budget");}
    {Fixture f;f.tables.values.resize(14*28);check(f.decode().state==State::invalid,"truncated child table rejected without indexing beyond copied bytes");}
    {Fixture f;f.tables.objects.resize(30*8);check(f.decode().state==State::invalid,"truncated object table rejected");}
    {Fixture f;f.names.erase(7);check(f.decode().state==State::invalid,"unknown canonical field rejected");}
    {Fixture f;f.strings.erase(50);check(f.decode().state==State::invalid,"unknown readiness string rejected");}
    {Fixture f;f.graph.map="maps/mp/zm_buried.d3dbsp";f.strings[10]="zm_buried";f.names[9]="ghost_rooms";f.names[10]="ghost_spawn_locations";f.object(22,21);f.object(23,18);f.object(24,21);f.object(32,18);f.child(3,9,1,22);f.child(22,70,1,23);f.child(23,10,1,24);f.child(24,0x800000,1,32);f.child(32,7,4,0x10040);f.vectors[0x10040]={9,8,7};auto result=f.decode();check(result.state==State::ready&&result.sites.size()==3&&result.sites[2].types==8&&!result.sites[2].active,"Authored ghost room origin needs no normal enabled field");check(result.active_count==1&&result.vectors.size()==3,"Ghost points preserve normal active count and second-read vectors");f.tag(23,10,7);check(f.decode().state==State::invalid,"Malformed ghost registry rejected");}
    {Fixture f;f.graph.map="maps/mp/zm_buried.d3dbsp";f.strings[10]="zm_buried";f.names[9]="ghost_front_standing_locations";f.object(22,21);f.child(3,9,1,22);f.child(22,0x800000,1,30);auto result=f.decode();check(result.state==State::ready&&result.sites[0].types==9&&result.active_count==1,"Ghost entrance shared normal site retains both identities");}
    {Fixture f;f.names[9]="ghost_rooms";f.child(3,9,7,0);check(f.decode().state==State::ready,"Ghost registries ignored outside Buried");}
    {Fixture f;f.graph.map="maps/mp/zm_tomb.d3dbsp";f.strings[10]="zm_tomb";f.names[9]="mechz_locations";f.object(22,21);f.child(20,9,1,22);f.child(22,0x800000,1,30);auto result=f.decode();check(result.state==State::ready&&result.sites[0].types==17&&result.active_count==1,"Panzer shared normal spawn preserves both identities and normal pool status");}
    {Fixture f;f.graph.map="maps/mp/zm_tomb.d3dbsp";f.strings[10]="zm_tomb";f.names[9]="zone_capture";f.names[10]="zones";f.names[11]="struct_class_names";f.names[12]="target";f.object(50,18);f.object(51,21);f.object(52,18);f.object(53,21);f.object(54,21);f.object(55,21);f.object(32,18);f.child(3,9,1,50);f.child(50,10,1,51);f.child(51,200,1,52);f.child(52,12,2,201);f.child(3,11,1,53);f.child(53,202,1,54);f.child(54,201,1,55);f.child(55,0x800000,1,32);f.child(32,7,4,0x10040);f.strings[200]="generator_1";f.strings[201]="generator_holes";f.strings[202]="targetname";f.vectors[0x10040]={9,8,7};auto result=f.decode();check(result.state==State::ready&&result.sites.size()==3&&result.sites[2].types==32&&!result.sites[2].active,"Templar holes resolved before generator activation without normal enabled field");check(result.active_count==1&&result.vectors.size()==3,"Templar metadata preserves active count and vector second-read guards");f.names[13]="a_emergence_hole_structs";f.child(52,13,1,55);f.tag(3,11,7);check(f.decode().state==State::ready,"Cached generator holes do not require targetname fallback");f.tag(55,0x800000,7);check(f.decode().state==State::invalid,"Malformed Templar hole rejected");}
    check(spawns::site_label(49)=="Zombie\nPanzer\nTemplar","Shared labels preserve normal and multiple special identities");
    check(spawns::visible_types(49,false)==1&&spawns::visible_types(48,false)==0,"Special toggle hides Panzer and Templar while preserving shared normal icon");
    {Fixture f;f.graph.map="maps/mp/zm_prison.d3dbsp";f.strings[10]="zm_prison";f.names[9]="brutus_spawn_positions";f.object(22,21);f.object(32,18);f.child(3,9,1,22);f.child(22,0x800000,1,30);f.child(22,0x800001,1,32);f.child(32,7,4,0x10040);f.vectors[0x10040]={9,8,7};auto r=f.decode();check(r.state==State::ready&&r.sites.size()==3&&r.sites[0].types==65&&r.sites[2].types==64,"Brutus authored registry preserves shared normal identity and special-only points");check(r.active_count==1&&!r.sites[2].active,"Brutus points do not invent normal pool membership");f.tag(22,0x800001,7);check(f.decode().state==State::invalid,"Malformed Brutus point rejects capture");}
    {Fixture f;f.names[9]="brutus_spawn_positions";f.child(3,9,7,0);check(f.decode().state==State::ready,"Brutus registry ignored outside Mob");}
    check(spawns::site_label(65)=="Zombie\nBrutus"&&spawns::visible_types(65,false)==1&&spawns::visible_types(64,false)==0,"Brutus labels and special toggle preserve shared normal icon");
    std::printf("Native spawn decoder checks: %s (%d checks, %d failures)\n",failures?"FAIL":"PASS",checks,failures);return failures?1:0;
}
