#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <array>
#include <cstring>
#include <mutex>
#include "memory.hpp"
#include "despawn_schedule.hpp"
#include "spawns_batch.hpp"
#include "spawns_native_fingerprints.hpp"
namespace despawn::schedule {
namespace {
std::mutex guard;
bool ready=false;
std::uint32_t first=0,last=0,header=0,level_bound=0,objects_bound=0,bound_function=0,bound_replacement=0;
// Replacement tracker (script::export) when a replacefunc runs the loop, else empty.
std::string source;
float configured=-1;
std::shared_ptr<const nav::Graph> bound_map;
Sample current;
bool read(std::uint32_t a,void* p,std::size_t n) {if(a<0x10000||n>4096||n>0xFFFFFFFFu-a)return false;return memory::read(a,p,n);}
template<class T> bool read(std::uint32_t a,T& p) {return read(a,&p,sizeof(p));}
template<class T> T at(const unsigned char* p) {T out{};std::memcpy(&out,p,sizeof(out));return out;}
bool text(std::uint32_t a,std::string& s) {s.clear();for(unsigned i=0;i<128;++i) {char c=0;if(!read(a+i,c))return false;if(!c)return true;if(c<32||c>126)return false;s+=c;}return false;}
const char* script(const std::string& map) {
 if(map=="maps/mp/zm_transit.d3dbsp")return "maps/mp/zm_transit_distance_tracking.gsc";
 if(map=="maps/mp/zm_highrise.d3dbsp")return "maps/mp/zm_highrise_distance_tracking.gsc";
 if(map=="maps/mp/zm_prison.d3dbsp")return "maps/mp/zm_alcatraz_distance_tracking.gsc";
 if(map=="maps/mp/zm_buried.d3dbsp")return "maps/mp/zm_buried_distance_tracking.gsc";
 if(map=="maps/mp/zm_tomb.d3dbsp")return "maps/mp/zm_tomb_distance_tracking.gsc";
 return nullptr;
}
// Resolve the loaded GSC from a VM function value; no session IDs are persisted.
// With expected set, the value must be that script's zombie_tracking_init export.
// Without it (a replacefunc replacement from getdetour), any loaded script's export
// that starts exactly at the value is accepted, and its script and export name are
// returned; the waiting-thread and deadline checks in update() are unchanged.
bool resolve(std::uint32_t function,const char* expected,std::string* found=nullptr) {
 constexpr unsigned char magic[]={0x80,0x47,0x53,0x43,0x0D,0x0A,0x00,0x06};
 const auto page=function&~4095u;
 for(unsigned distance=0;distance<=0x40000&&page>=0x10000+distance;distance+=4096) {
  const auto base=page-distance;std::array<unsigned char,4096> bytes{};
  if(!read(base,bytes.data(),bytes.size()))continue;
  for(unsigned i=0;i+sizeof(magic)<=bytes.size();++i) {
   if(std::memcmp(bytes.data()+i,magic,sizeof(magic)))continue;
   const auto a=base+i;std::array<unsigned char,68> h{};if(!read(a,h))continue;
   const auto code=at<std::uint32_t>(h.data()+20),size=at<std::uint32_t>(h.data()+44),exports=at<std::uint32_t>(h.data()+28);
   const auto count=at<std::uint16_t>(h.data()+52),name=at<std::uint16_t>(h.data()+48);
   if(!size||size>0x400000||code>0x400000||exports>0x400000||a>0xFFFFFFFFu-0x800000||!count||count>4096||function<a+code||function>=a+code+size)continue;
   std::string s,script_name;if(!text(a+name,script_name)||(expected&&script_name!=expected))continue;
   bool matched=false;auto end=a+code+size;
   for(unsigned e=0;e<count;++e) {
    std::array<unsigned char,12> x{};if(!read(a+exports+e*12,x))return false;
    const auto offset=at<std::uint32_t>(x.data()+4);if(offset<code||offset>=code+size)return false;
    const auto address=a+offset;if(address>function)end=std::min(end,address);
    if(address==function) {
     if(!text(a+at<std::uint16_t>(x.data()+8),s)||x[10]!=0||(expected&&s!="zombie_tracking_init"))return false;
     matched=true;if(found)*found=script_name+"::"+s;
    }
   }
   if(matched&&end>function) {first=function;last=end;header=a;return true;}
  }
 }
 return false;
}
struct Child {std::uint32_t key=0,payload=0;unsigned tag=0;};
bool walk(std::uint32_t id,std::uint32_t objects,std::uint32_t children,std::uint32_t values,std::vector<Child>& out) {
 out.clear();if(!id||id>=32768)return false;
 std::array<unsigned char,8> object{},links{};if(!read(objects+id*8,object)||!read(children+id*8,links))return false;
 const auto kind=at<std::uint32_t>(object.data()+4)&127u;if(kind!=18&&kind!=21)return false;
 const auto tail=at<std::uint32_t>(links.data()+4);auto next=at<std::uint32_t>(links.data());std::uint32_t previous=0;
 while(next) {
  if(next>=262144||out.size()>=8192)return false;
  std::array<unsigned char,28> c{};if(!read(values+next*28,c)||at<std::uint16_t>(c.data()+18)!=id||at<std::uint32_t>(c.data()+24)!=previous)return false;
  out.push_back({static_cast<std::uint32_t>(c[13])+(static_cast<std::uint32_t>(at<std::uint16_t>(c.data()+16))<<8),at<std::uint32_t>(c.data()),c[12]});
  previous=next;next=at<std::uint32_t>(c.data()+20);
 }
 std::array<unsigned char,8> check{};return previous==tail&&valid_container(kind,at<std::uint16_t>(object.data()+2),out.size())&&read(children+id*8,check)&&check==links&&read(objects+id*8,check)&&check==object;
}
void reset() {first=last=header=level_bound=objects_bound=bound_function=bound_replacement=0;configured=-1;bound_map.reset();current={};source.clear();}
}
bool startup() {std::lock_guard lock(guard);reset();ready=true;for(const auto& f:spawns::native::spawns_native_fingerprints) {std::array<unsigned char,256> b{};if(f.size>b.size()||!read(f.address,b.data(),f.size)||std::memcmp(b.data(),f.bytes,f.size))ready=false;}return ready;}
void clear() {std::lock_guard lock(guard);reset();}
void bind(std::uint32_t function,float interval,std::uint32_t replacement) {
 std::lock_guard lock(guard);auto map=nav::graph();std::uint32_t level=0,objects=0;
 if(!ready||!map||!std::isfinite(interval)||interval<=0||interval>120||!read(0x2DEA81C,level)||!read(0x2DEA400,objects)) {reset();return;}
 const auto expected=script(map->map);if(!expected) {reset();current.status="No periodic tracker identified for this map";return;}
 if(!bound_map||!spawns::same_map(*bound_map,*map)||bound_function!=function||bound_replacement!=replacement||level_bound!=level||objects_bound!=objects) {
  reset();
  // The vanilla export must validate first: it ties the binding to this map's tracker.
  if(!resolve(function,expected)) {current.status="Tracker function binding could not be validated";return;}
  std::string found;
  if(replacement&&replacement!=function&&!resolve(replacement,nullptr,&found)) {reset();current.status="Tracker replacement could not be validated";return;}
  bound_map=map;level_bound=level;objects_bound=objects;bound_function=function;bound_replacement=replacement;source=found;
 }
 configured=interval;
}
void update() {
 std::lock_guard lock(guard);current={};current.map=bound_map;current.interval=configured;current.source=source;
 if(!ready) {current.status="Scheduler VM fingerprints mismatch";return;}
 if(!bound_map||!first)return;
 nav::Camera camera{};auto map=nav::graph();if(!map||!spawns::same_map(*map,*bound_map)) {reset();return;}
 if(!nav::read_camera(camera,*bound_map)) {current.status="Camera temporarily unavailable";return;}
 std::uint32_t level=0,objects=0,children=0,values=0,clock=0,waits=0;
 if(!read(0x2DEA81C,level)||!read(0x2DEA400,objects)||!read(0x2DEA480,children)||!read(0x2DEA580,values)||!read(0x2DEA810,clock)||!read(0x2DEA814,waits)||level!=level_bound||objects!=objects_bound||!level||level>=32768||objects<0x10000||objects>0xFFFFFFFFu-0x40000||children<0x10000||children>0xFFFFFFFFu-0x40000||values<0x10000||values>0xFFFFFFFFu-0x700000) {reset();return;}
 // Revalidate the loaded header each capture to reject freed/reused scripts.
 std::array<unsigned char,8> magic{};constexpr unsigned char expected[]={0x80,0x47,0x53,0x43,0x0D,0x0A,0x00,0x06};
 if(!read(header,magic)||std::memcmp(magic.data(),expected,8)) {reset();return;}
 std::vector<Child> times,stacks;if(!walk(waits,objects,children,values,times)) {current.status="Scheduler wait registry unavailable";return;}
 unsigned matches=0;std::uint32_t remaining=0,next_deadline=0;
 for(const auto& t:times) {
  if(t.tag!=1)continue;
  if(!walk(t.payload,objects,children,values,stacks)) {current.status="Scheduler wait registry changed";return;}
  for(const auto& s:stacks) {
   if(s.tag!=11)continue;
   std::array<unsigned char,12> stack{};if(!read(s.payload,stack))return;
   const auto pc=at<std::uint32_t>(stack.data());if(pc<first||pc>=last)continue;
   if(s.key<=0x10000u||s.key>=0x18000u)return;const auto id=s.key-0x10000u;
   std::array<unsigned char,8> object{},again{};if(!read(objects+id*8,object))return;
   const auto kind=at<std::uint32_t>(object.data()+4),deadline=kind>>8;
   if((kind&127)!=16)return;
   const auto owner=at<std::uint16_t>(object.data()+2);
   if(owner!=level)return;
   // Scheduler time keys are raw 24-bit ticks; thread keys encode object IDs.
   if(t.key!=deadline)return;
   const auto ticks=remaining_ticks(deadline,clock);
   if(ticks>static_cast<std::uint32_t>(std::ceil(configured*20.0f))+1||!read(objects+id*8,again)||again!=object||!read(s.payload,again)||std::memcmp(again.data(),stack.data(),8))return;
   if(!matches||ticks<remaining) {remaining=ticks;next_deadline=deadline;}++matches;
  }
 }
 std::uint32_t clock_after=0,waits_after=0,level_after=0,objects_after=0;
 if(!read(0x2DEA810,clock_after)||clock_after!=clock||!read(0x2DEA814,waits_after)||waits_after!=waits||!read(0x2DEA81C,level_after)||level_after!=level||!read(0x2DEA400,objects_after)||objects_after!=objects)return;
 if(matches!=1) {current.status=matches?"Ambiguous tracking threads":"Waiting for first scheduled tracking check";return;}
 current.clock=clock&0xFFFFFFu;current.deadline=next_deadline;current.remaining=static_cast<float>(remaining)*0.05f;current.updated=GetTickCount64();current.status="Scheduled tracking check (VM deadline)";
}
Sample sample() {std::lock_guard lock(guard);return current;}
}
