#pragma once
#include "despawn.hpp"
#include "despawn_schedule.hpp"

// Development-only shadow contracts. No provider selection or HUD takeover.
namespace despawn::shadow {
enum class Origin {observer_assisted,native_independent};
enum class Verdict {match,mismatch,unavailable,inconclusive};
struct Context {
 std::shared_ptr<const nav::Graph> map;
 std::uint64_t generation=0;
 std::uint32_t tick=0;
 bool aligned=false;
};
struct Tracker {bool available=false;std::uint32_t deadline=0;float interval=-1;};
struct Actor {int entity=-1;std::uint64_t lifetime=0;bool lifetime_known=false;Category category=Category::unknown;int special=0;};
struct Sample {
 Context context;
 Origin origin=Origin::observer_assisted;
 Tracker tracker;
 bool actors_available=false;
 std::vector<Actor> actors;
 // Cumulative requests are not confirmed removals or replacement counts.
 bool deletion_requests_available=false;
 unsigned deletion_requests=0;
 bool replacements_available=false;
 unsigned replacements=0;
};
struct Comparison {
 Verdict tracker=Verdict::unavailable,actors=Verdict::unavailable;
 // Aggregate telemetry alone cannot establish matching event causes.
 Verdict deletion_requests=Verdict::unavailable,replacements=Verdict::unavailable;
};
inline bool aligned(const Sample& a,const Sample& b) {
 return a.context.map&&b.context.map&&spawns::same_map(*a.context.map,*b.context.map)
  &&a.context.generation!=0&&a.context.generation==b.context.generation
  &&a.context.aligned&&b.context.aligned&&a.context.tick==b.context.tick
  &&a.origin!=b.origin;
}
inline bool valid_actors(const std::vector<Actor>& actors) {
 std::array<bool,1024> seen{};
 for(const auto& a:actors) {
  if(a.entity<0||a.entity>=1024||seen[a.entity]||!a.lifetime_known||!a.lifetime)return false;
  seen[a.entity]=true;
 }
 return true;
}
inline Comparison compare(const Sample& baseline,const Sample& native) {
 Comparison result;
 if(baseline.tracker.available&&native.tracker.available) {
  result.tracker=Verdict::inconclusive;
  if(aligned(baseline,native)&&std::isfinite(baseline.tracker.interval)&&std::isfinite(native.tracker.interval)
    &&baseline.tracker.interval>0&&native.tracker.interval>0)
   result.tracker=baseline.tracker.deadline==native.tracker.deadline&&baseline.tracker.interval==native.tracker.interval?Verdict::match:Verdict::mismatch;
 }
 if(baseline.actors_available&&native.actors_available) {
  result.actors=Verdict::inconclusive;
  if(aligned(baseline,native)&&valid_actors(baseline.actors)&&valid_actors(native.actors)) {
   bool same=baseline.actors.size()==native.actors.size();
   for(const auto& a:baseline.actors) {
    const auto found=std::find_if(native.actors.begin(),native.actors.end(),[&](const Actor& b){return a.entity==b.entity;});
    if(found==native.actors.end()) {same=false;continue;}
    if(a.lifetime!=found->lifetime) {result.actors=Verdict::inconclusive;return result;}
    if(a.category!=found->category||a.special!=found->special)same=false;
   }
   result.actors=same?Verdict::match:Verdict::mismatch;
  }
 }
 if(baseline.deletion_requests_available&&native.deletion_requests_available)result.deletion_requests=Verdict::inconclusive;
 if(baseline.replacements_available&&native.replacements_available)result.replacements=Verdict::inconclusive;
 return result;
}
// Adapter deliberately cannot invent tick alignment or actor lifetime identity.
inline Sample observer_sample(const std::shared_ptr<const Snapshot>& actors,const Events& events,const schedule::Sample& timer) {
 Sample out;
 out.context.map=actors?actors->map:timer.map;
 out.context.tick=timer.clock;
 if(timer.map&&out.context.map&&spawns::same_map(*timer.map,*out.context.map)&&timer.remaining>=0&&timer.interval>0)
  out.tracker={true,timer.deadline,timer.interval};
 if(actors&&actors->map) {
  out.actors_available=true;
  for(const auto& a:actors->entries)out.actors.push_back({a.entity,0,false,a.category,a.special});
 }
 if(events.map&&out.context.map&&spawns::same_map(*events.map,*out.context.map)) {
  out.deletion_requests_available=true;out.deletion_requests=events.notifications;
 }
 return out;
}
// Until independent VM discovery exists, absence must remain explicit.
inline Sample native_unavailable(const Context& context) {
 Sample out;out.context=context;out.origin=Origin::native_independent;return out;
}
}