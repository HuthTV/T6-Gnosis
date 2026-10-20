#pragma once
#include <string>
#include "navigation.hpp"
namespace despawn::schedule {
// source names the replacement tracker (script::export) when a replacefunc runs the loop.
struct Sample {std::shared_ptr<const nav::Graph> map;float remaining=-1,interval=-1;std::uint32_t clock=0,deadline=0;std::uint64_t updated=0;const char* status="Waiting for tracker initialization";std::string source;};
// Outcome readout: observed deletion requests in the helper's delayed result
// window, and the user's requested in-view snapshot at the pass boundary.
// These are not authoritative replacement-pool or causal prevention counters.
struct PassResults {
 bool initialized=false,pending=false,available=false,collecting=false;
 std::uint32_t deadline=0,start=0;
 unsigned previous=0,baseline=0,in_view=0,despawned=0,prevented=0;
 void observe(std::uint32_t clock,std::uint32_t next,unsigned notifications,unsigned visible) {
  if(initialized&&notifications<previous)*this={};
  if(!initialized) {initialized=true;deadline=next;previous=notifications;return;}
  if(next!=deadline) {const bool due=((clock-deadline)&0xFFFFFFu)<0x800000u;deadline=next;if(due) {available=false;start=clock;baseline=previous;in_view=visible;pending=true;collecting=true;}}
  if(collecting&&((clock-start)&0xFFFFFFu)>=3) {despawned=notifications-baseline;prevented=in_view;available=despawned>0||prevented>0;pending=false;}
  previous=notifications;
 }
};
bool startup();
void clear();
// replacement: the active replacefunc target from getdetour, or 0 for the vanilla loop.
void bind(std::uint32_t function,float interval,std::uint32_t replacement=0);
void update();
Sample sample();
inline bool valid_container(unsigned kind,unsigned size,std::size_t count) {return kind==18||(kind==21&&size==count);}
inline std::uint32_t remaining_ticks(std::uint32_t deadline,std::uint32_t clock) {return (deadline-clock)&0xFFFFFFu;}
}
