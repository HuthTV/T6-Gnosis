#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <array>
#include <cstring>
#include <mutex>
#include "memory.hpp"
#include "navigation.hpp"
#include "fingerprints.hpp"

namespace nav {
namespace {
std::mutex guard;
std::shared_ptr<const Graph> current;
const char* state="Waiting for map";
bool ready=false;
constexpr std::uint32_t path_pointer=0xD65774, node_count=0x2E05284, cg_pointer=0x1113F9C;
constexpr std::uint32_t max_nodes=16384, max_links=256, max_total_links=1000000;
bool read(std::uint32_t address,void* output,std::size_t bytes) {
    if(address<0x10000||bytes>0x400000||bytes>0xFFFFFFFFu-address)return false;
    return memory::read(address,output,bytes);
}
template<class T> bool read(std::uint32_t address,T& out) { return read(address,&out,sizeof(out)); }
template<class T> T field(const unsigned char* data,std::size_t offset) {
    T value{};std::memcpy(&value,data+offset,sizeof(value));return value;
}
struct Identity { std::uint32_t data=0,base=0,count=0;std::array<char,160> map{}; };
bool identity(Identity& i) {
    if(!read(path_pointer,i.data)||!i.data)return false;
    std::array<std::uint32_t,4> header{};
    if(!read(i.data,header)||!read(node_count,i.count)||i.count==0||i.count>max_nodes||header[1]!=i.count||header[2]!=i.count)return false;
    i.base=header[3];
    if(!i.base||!read(header[0],i.map))return false;
    if(!std::memchr(i.map.data(),0,i.map.size()))return false;
    // Only the observed T6 Zombies BSP naming contract is supported here.
    return std::strncmp(i.map.data(),"maps/mp/zm_",11)==0 && std::strstr(i.map.data(),".d3dbsp")!=nullptr;
}
bool same(const Identity& a,const Identity& b) {
    return a.data==b.data&&a.base==b.base&&a.count==b.count&&a.map==b.map;
}
void fail(const char* why) { std::lock_guard lock(guard);current.reset();state=why; }
}
bool bindings_match() {
    ready=true;
    for(const auto& f:fingerprints) {
        std::vector<unsigned char> bytes(f.size);
        if(!read(f.address,bytes.data(),bytes.size())||std::memcmp(bytes.data(),f.bytes,f.size)) {ready=false;break;}
    }
    if(!ready)fail("Engine binding mismatch; drawing disabled");
    return ready;
}
void clear_graph(int) { fail("Waiting for map / restart"); }
std::shared_ptr<const Graph> graph() { std::lock_guard lock(guard);return current; }
const char* status() { std::lock_guard lock(guard);return state; }
void refresh_graph() {
    if(!ready)return;
    Identity before{},after{};
    if(!identity(before)) {fail("No valid loaded Zombies graph");return;}
    // Clear an old map before gathering the replacement snapshot.
    {std::lock_guard lock(guard);if(current&&(current->base!=before.base||current->data!=before.data||current->count!=before.count||current->map!=before.map.data()))current.reset();}
    std::vector<unsigned char> raw(static_cast<std::size_t>(before.count)*0x90);
    if(!read(before.base,raw.data(),raw.size())) {fail("Node snapshot unreadable");return;}
    auto next=std::make_shared<Graph>();
    next->data=before.data;next->base=before.base;next->count=before.count;next->map=before.map.data();next->nodes.resize(before.count);
    for(std::uint32_t index=0;index<before.count;++index) {
        const auto* record=raw.data()+index*0x90;
        auto& n=next->nodes[index];n.origin=field<Vec3>(record,0x18);n.type=field<std::uint16_t>(record,0);
        const auto count=field<std::uint16_t>(record,0x3C);const auto pointer=field<std::uint32_t>(record,0x40);
        if(!finite(n.origin)||std::abs(n.origin.x)>1000000||std::abs(n.origin.y)>1000000||std::abs(n.origin.z)>1000000||count>max_links||(count&&!pointer)) {fail("Invalid node/link metadata");return;}
        next->directed_links+=count;
        if(next->directed_links>max_total_links) {fail("Graph link budget exceeded");return;}
        std::array<unsigned char,max_links*16> links{};
        if(count&&!read(pointer,links.data(),count*16u)) {fail("Link snapshot unreadable");return;}
        n.links.reserve(count);
        for(unsigned j=0;j<count;++j) {
            const auto target=field<std::uint16_t>(links.data()+j*16,4);
            if(target>=before.count) {fail("Link destination outside node array");return;}
            n.links.push_back(target);
        }
    }
    if(!identity(after)||!same(before,after)) {fail("Map changed during capture");return;}
    std::lock_guard lock(guard);current=std::move(next);state="Static graph ready";
}
bool read_camera_once(Camera& out,const Graph& g) {
    if(!ready)return false;
    Identity i{};
    if(!identity(i)||i.data!=g.data||i.base!=g.base||i.count!=g.count||g.map!=i.map.data())return false;
    std::uint32_t cg=0,snapshot=0;
    if(!read(cg_pointer,cg)||!cg||!read(cg+0x28,snapshot)||!snapshot)return false;
    std::array<unsigned char,0x68> raw{};
    if(!read(cg+0x4D890,raw))return false;
    Camera c{};c.x=field<int>(raw.data(),0);c.y=field<int>(raw.data(),4);c.width=field<int>(raw.data(),8);c.height=field<int>(raw.data(),12);
    c.tan_x=field<float>(raw.data(),0x20);c.tan_y=field<float>(raw.data(),0x24);
    c.origin=field<Vec3>(raw.data(),0x34);c.forward=field<Vec3>(raw.data(),0x44);c.left=field<Vec3>(raw.data(),0x50);c.up=field<Vec3>(raw.data(),0x5C);
    std::uint32_t verify=0;
    if(!valid_camera(c)||c.x!=0||c.y!=0||!read(cg_pointer,verify)||verify!=cg||!read(cg+0x28,verify)||verify!=snapshot)return false;
    out=c;return true;
}
bool read_camera(Camera& out,const Graph& g) {
    for(unsigned attempt=0;attempt<3;++attempt)if(read_camera_once(out,g))return true;
    return false;
}

}
