#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <mutex>
#include "memory.hpp"
#include "spawns_native.hpp"
#include "spawns_native_fingerprints.hpp"

namespace spawns::native {
namespace {
std::mutex guard;
bool ready=false;
struct Identity {
    std::uint32_t level=0,objects=0,children=0,values=0,strings=0,canonical=0,limit=0;
    bool operator==(const Identity& b) const {return level==b.level&&objects==b.objects&&children==b.children&&values==b.values&&strings==b.strings&&canonical==b.canonical&&limit==b.limit;}
};
Identity cached;
std::shared_ptr<const nav::Graph> cached_map;
std::unordered_map<std::uint32_t,std::string> names,strings;
bool read(std::uint32_t address,void* output,std::size_t size) {
    if(address<0x10000||size>0x800000||size>0xFFFFFFFFu-address)return false;
    // Large table copies are read in bounded chunks.
    auto* out=static_cast<unsigned char*>(output);std::size_t offset=0;
    while(offset<size) {
        const auto bytes=std::min<std::size_t>(size-offset,0x400000);
        if(!memory::read(address+static_cast<std::uint32_t>(offset),out+offset,bytes))return false;
        offset+=bytes;
    }
    return true;
}
template<class T> bool read(std::uint32_t address,T& out) {return read(address,&out,sizeof(out));}
bool text(std::uint32_t address,std::string& out) {
    out.clear();
    for(unsigned offset=0;offset<128;) {
        std::array<unsigned char,128> chunk{};
        const auto count=std::min<unsigned>(128-offset,4096-((address+offset)&4095));
        if(address>0xFFFFFFFFu-offset||!read(address+offset,chunk.data(),count))return false;
        for(unsigned i=0;i<count;++i) {if(!chunk[i])return true;if(chunk[i]<32||chunk[i]>126)return false;out.push_back(static_cast<char>(chunk[i]));}
        offset+=count;
    }
    return false;
}
bool identity(Identity& i) {
    std::array<unsigned char,8> limit{},table{};
    if(!read(0x2DEA81C,i.level)||!read(0x2DEA400,i.objects)||!read(0x2DEA480,i.children)||!read(0x2DEA580,i.values)||!read(0x2BF2DA4,i.strings)||!read(0x51A9A1,limit)||!read(0x5466C5,table))return false;
    if(limit[0]!=0x81||limit[1]!=0xFE||limit[6]!=0x72||table[0]!=0x89||table[1]!=0x1C||table[2]!=0xB5)return false;
    std::memcpy(&i.limit,limit.data()+2,4);std::memcpy(&i.canonical,table.data()+3,4);
    return i.level>0&&i.level<32768&&i.objects>=0x10000&&i.objects<=0xFFFFFFFFu-0x40000&&
        i.children>=0x10000&&i.children<=0xFFFFFFFFu-0x40000&&i.values>=0x10000&&i.values<=0xFFFFFFFFu-0x700000&&
        i.strings>=0x10000&&i.canonical>=0x10000&&i.limit>1&&i.limit<=65536&&i.canonical<=0xFFFFFFFFu-i.limit*4;
}
void reset_cache() {cached={};cached_map.reset();names.clear();strings.clear();}
}
bool startup() {
    std::lock_guard lock(guard);reset_cache();ready=true;
    for(const auto& f:spawns_native_fingerprints) {
        std::array<unsigned char,256> raw{};
        if(f.size>raw.size()||!read(f.address,raw.data(),f.size)||std::memcmp(raw.data(),f.bytes,f.size)) {ready=false;break;}
    }
    return ready;
}
void clear() {std::lock_guard lock(guard);reset_cache();}
Capture collect(std::shared_ptr<const nav::Graph> map) {
    std::lock_guard lock(guard);Capture out;nav::Camera camera{};Identity before{},after{};
    if(!ready)return out;
    if(!map||!nav::read_camera(camera,*map)||!identity(before)) {reset_cache();out.reason="Waiting for valid native spawn map";return out;}
    if(!(before==cached)||!cached_map||!same_map(*map,*cached_map)) {reset_cache();cached=before;cached_map=map;}
    Tables tables;tables.objects.resize(0x40000);tables.children.resize(0x40000);tables.values.resize(0x700000);
    if(!read(before.objects,tables.objects.data(),tables.objects.size())||!read(before.children,tables.children.data(),tables.children.size())||!read(before.values,tables.values.data(),tables.values.size())) {reset_cache();out.reason="Native spawn tables unreadable";return out;}
    Read reader=[](std::uint32_t a,void* p,std::size_t n) {return read(a,p,n);};
    Name canonical=[&](std::uint32_t key,std::string& name) {
        if(!key||key>=before.limit)return false;
        const auto found=names.find(key);if(found!=names.end()) {name=found->second;return true;}
        std::uint32_t pointer=0;if(!read(before.canonical+key*4,pointer)||!pointer||!text(pointer,name))return false;
        if(names.size()>=8192)return false;
        names.emplace(key,name);return true;
    };
    Name ordinary=[&](std::uint32_t key,std::string& name) {
        if(!key||key>=0x40000||before.strings>0xFFFFFFFFu-key*24u-4u)return false;
        // Names in the flag registry are stable for the current VM session.
        // The actual map string is additionally re-read before publication.
        const auto found=strings.find(key);if(found!=strings.end()) {name=found->second;return true;}
        if(!text(before.strings+key*24u+4u,name)||strings.size()>=8192)return false;
        strings.emplace(key,name);return true;
    };
    Decoder decoder(tables,reader,canonical,ordinary,before.limit);auto decoded=decoder.decode(before.level,*map);
    if(!identity(after)||!(before==after)||!nav::read_camera(camera,*map)) {reset_cache();out.reason="Native spawn session changed during capture";return out;}
    // Re-read the level's table records and readiness from a second local view.
    // Game-thread execution avoids VM execution between reads; these checks also
    // reject a teardown or allocation change rather than retaining raw pointers.
    std::array<unsigned char,8> level_links{},level_object{};
    if(!read(before.children+before.level*8,level_links)||!read(before.objects+before.level*8,level_object)||
       std::memcmp(level_links.data(),tables.children.data()+before.level*8,8)||std::memcmp(level_object.data(),tables.objects.data()+before.level*8,8)) {reset_cache();out.reason="Native level changed during capture";return out;}
    out.state=decoded.state;out.reason=decoded.reason;
    if(decoded.state==State::invalid) {reset_cache();return out;}
    if(decoded.state==State::busy)return out;
    // Validate all pages containing visited object metadata and child links,
    // including the full active-pool array. In-place mutation can retain its ID.
    const std::array<const std::vector<unsigned char>*,3> sources{{&tables.objects,&tables.children,&tables.values}};
    const std::array<std::uint32_t,3> bases{{before.objects,before.children,before.values}};
    for(unsigned table=0;table<3;++table)for(auto page:decoded.pages[table]) {
        const auto offset=static_cast<std::size_t>(page)*4096;std::array<unsigned char,4096> bytes{};
        if(offset>=sources[table]->size()) {reset_cache();out.state=State::invalid;out.reason="Native spawn validation page invalid";return out;}
        const auto size=std::min<std::size_t>(4096,sources[table]->size()-offset);
        if(!read(bases[table]+static_cast<std::uint32_t>(offset),bytes.data(),size)||std::memcmp(bytes.data(),sources[table]->data()+offset,size)) {reset_cache();out.state=State::invalid;out.reason="Native spawn registry changed during capture";return out;}
    }
    for(const auto& vector:decoded.vectors) {
        nav::Vec3 value{};
        if(!read(vector.address,value)||std::memcmp(&value,&vector.value,sizeof(value))) {reset_cache();out.state=State::invalid;out.reason="Native spawn origin changed during capture";return out;}
    }
    std::string actual_map;
    if(!text(before.strings+decoded.script_token*24u+4u,actual_map)||!map_name_matches(*map,actual_map)||!identity(after)||!(before==after)||!nav::read_camera(camera,*map)) {reset_cache();out.state=State::invalid;out.reason="Native spawn identity changed before publication";return out;}
    auto next=std::make_shared<Snapshot>();next->map=map;next->sites=std::move(decoded.sites);next->active_count=decoded.active_count;next->updated=GetTickCount64();out.data=std::move(next);return out;
}
}
