#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <plutonium_sdk.hpp>
#include <atomic>
#include <mutex>
#include <cstdio>
#include "memory.hpp"
#include "despawn.hpp"
#include "triggers.hpp"
#include "despawn_schedule.hpp"
#include "despawn_bridge.hpp"
#include "spawn_fingerprints.hpp"

namespace despawn {
namespace {
std::mutex guard;
std::atomic<bool> ready{false};
Batch batch;
std::shared_ptr<const Snapshot> current;
Events observed;
bool read(std::uint32_t address,void* out,std::size_t size) {
    if(address<0x10000||size>4096||size>0xFFFFFFFFu-address)return false;
    return memory::read(address,out,size);
}
template<class T> bool read(std::uint32_t address,T& out) {return read(address,&out,sizeof(out));}
struct Value {std::uint32_t type=0,payload=0;};
struct Arguments {
    std::array<Value,4> slots{};
    std::uint32_t count=0,top=0;
    bool unchanged() const {
        std::uint32_t n=0,p=0;return read(0x2DEA8EC,n)&&read(0x2DEA8E0,p)&&n==count&&p==top;
    }
    bool capture(std::uint32_t expected) {
        constexpr std::uint32_t first=0x2DEAB70,last=0x2DEEB68;
        if(expected>slots.size()||!read(0x2DEA8EC,count)||count!=expected||!read(0x2DEA8E0,top)||
            top<first||top>last||((top-first)&7u)||(expected&&top-first<(expected-1)*8))return false;
        for(unsigned i=0;i<expected;++i)if(!read(top-i*8,slots[i]))return false;
        return unchanged();
    }
    bool integer(unsigned index,int& out) const {
        if(index>=count||slots[index].type!=7)return false;
        std::memcpy(&out,&slots[index].payload,4);return true;
    }
    bool number(unsigned index,float& out) const {
        if(index>=count)return false;
        if(slots[index].type==6)std::memcpy(&out,&slots[index].payload,4);
        else {int value=0;if(!integer(index,value))return false;out=static_cast<float>(value);}
        return std::isfinite(out);
    }
    bool text(unsigned index,std::array<char,128>& out) const {
        std::uint32_t table=0;
        if(index>=count||slots[index].type!=2||!slots[index].payload||slots[index].payload>=0x40000||!read(0x2BF2DA4,table)||table<0x10000||table>0xFFFFFFFFu-slots[index].payload*24u-4u)return false;
        const auto address=table+slots[index].payload*24u+4u;
        for(unsigned i=0;i<out.size();++i) {
            if(address>0xFFFFFFFFu-i||!read(address+i,out[i]))return false;
            if(!out[i])return unchanged();
            if(static_cast<unsigned char>(out[i])<32||static_cast<unsigned char>(out[i])>126)return false;
        }
        return false;
    }
    bool map(std::array<char,32>& out) const {
        std::uint32_t table=0;
        if(!count||slots[0].type!=2||!slots[0].payload||slots[0].payload>0xFFFFFF||!read(0x2BF2DA4,table)||
            table<0x10000||table>0xFFFFFFFFu-slots[0].payload*24u-4u)return false;
        const auto address=table+slots[0].payload*24u+4u;
        for(unsigned i=0;i<out.size();++i) {
            if(address>0xFFFFFFFFu-i||!read(address+i,out[i]))return false;
            if(!out[i])return unchanged();
        }
        return false;
    }
};
bool live(const std::shared_ptr<const nav::Graph>& map) {nav::Camera camera{};return map&&nav::read_camera(camera,*map);}
void begin_impl() {
    Arguments args;std::array<char,32> script{};int seq=0,count=0;float interval=0;
    if(!args.capture(4)||!args.map(script)||!args.integer(1,seq)||!args.integer(2,count)||!args.number(3,interval)||!args.unchanged()) {clear();return;}
    auto map=nav::graph();if(!live(map)) {clear();return;}
    std::lock_guard lock(guard);
    if(!batch.begin(map,script.data(),seq,count,interval))current.reset();
}
void entry_impl() {
    Arguments args;int seq=0,entity=0,state=0,special=0;
    if(!args.capture(4)||!args.integer(0,seq)||!args.integer(1,entity)||!args.integer(2,state)||!args.integer(3,special)||!args.unchanged()) {clear();return;}
    std::lock_guard lock(guard);if(!batch.entry(seq,entity,state,special))current.reset();
}
void commit_impl() {
    Arguments args;int seq=0,count=0;
    if(!args.capture(2)||!args.integer(0,seq)||!args.integer(1,count)||!args.unchanged()) {clear();return;}
    auto map=nav::graph();if(!live(map)) {clear();return;}
    std::lock_guard lock(guard);current=batch.commit(*map,seq,count,GetTickCount64());
}
template<void(*Callback)()> void invoke() noexcept {
    if(!ready.load())return;
    try {Callback();}catch(...) {clear();}
}
void PLUTONIUM_CALLBACK begin_callback() {invoke<begin_impl>();}
void PLUTONIUM_CALLBACK entry_callback() {invoke<entry_impl>();}
void PLUTONIUM_CALLBACK commit_callback() {invoke<commit_impl>();}
// GnosisTrackerBinding(vanilla_tracker, interval[, replacement]). The optional third
// argument is getdetour's result when a replacefunc (e.g. an O-Prac segment start)
// runs the loop instead; a non-function third value binds the vanilla loop only.
void tracker_impl() {
    Arguments args;float interval=0;
    if(args.capture(3)) {
        if(args.slots[0].type==10&&args.number(1,interval)&&args.unchanged())schedule::bind(args.slots[0].payload,interval,args.slots[2].type==10?args.slots[2].payload:0);
        return;
    }
    if(args.capture(2)&&args.slots[0].type==10&&args.number(1,interval)&&args.unchanged())schedule::bind(args.slots[0].payload,interval);
}
void PLUTONIUM_CALLBACK tracker_callback() {invoke<tracker_impl>();}
void trigger_impl() {
 Arguments args;int entity=0;std::array<char,128> classname{},key{},value{};
 if(args.capture(4)&&args.integer(0,entity)&&args.text(1,classname)&&args.text(2,key)&&args.text(3,value)&&args.unchanged())triggers::metadata(entity,classname.data(),key.data(),value.data());
}
void PLUTONIUM_CALLBACK trigger_callback() {invoke<trigger_impl>();}
}
bool startup(plutonium::sdk::iinterface* api) {
    ready=false;clear();schedule::startup();if(!api)return false;
    api->gsc()->register_function("GnosisTriggerField",trigger_callback);
    api->gsc()->register_function("GnosisDespawnBegin",begin_callback);
    api->gsc()->register_function("GnosisDespawnZombie",entry_callback);
    api->gsc()->register_function("GnosisDespawnCommit",commit_callback);
    api->gsc()->register_function("GnosisTrackerBinding",tracker_callback);
    for(const auto& f:spawns::spawn_fingerprints) {
        std::array<unsigned char,128> bytes{};
        if(f.size>bytes.size()||!read(f.address,bytes.data(),f.size)||std::memcmp(bytes.data(),f.bytes,f.size))return false;
    }
    ready=true;return true;
}
void clear() {schedule::clear();std::lock_guard lock(guard);batch.clear();current.reset();observed={};}
std::shared_ptr<const Snapshot> snapshot() {std::lock_guard lock(guard);return current;}
Events events() {std::lock_guard lock(guard);return observed;}
void zombie_deleted(const std::shared_ptr<const nav::Graph>& map,int entity) {
    if(!map)return;
    std::lock_guard lock(guard);
    if(!observed.map||!spawns::same_map(*observed.map,*map))observed={};
    observed.map=map;++observed.notifications;observed.last_entity=entity;observed.last_script_time=-1;observed.updated=GetTickCount64();
}
}
