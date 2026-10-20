#pragma once
#include "spawns_batch.hpp"
#include <array>

namespace despawn {
enum class Category : unsigned {unknown,candidate,proximity,view,exempt,proximity_and_view};
inline const char* name(Category c) {
    switch(c) {
    case Category::candidate:return "Rule candidate";
    case Category::proximity:return "Protected: nearby";
    case Category::view:return "Protected: view cone";
    case Category::exempt:return "Exempt / tracking disabled";
    case Category::proximity_and_view:return "Protected: nearby + view";
    default:return "Unknown";
    }
}
// Map-script identity: 0 ordinary/unidentified, 1 Denizen, 2 ghost, 3 Panzer, 4 leaper, 5 Avogadro.
struct Entry {int entity=0;Category category=Category::unknown;int special=0;};
struct Snapshot {
    std::shared_ptr<const nav::Graph> map;
    std::vector<Entry> entries;
    float interval=-1;
    std::uint64_t updated=0;
};
struct Events {std::shared_ptr<const nav::Graph> map;unsigned notifications=0;int last_entity=-1,last_script_time=-1;std::uint64_t updated=0;};
struct Phase {std::shared_ptr<const nav::Graph> map;int game_time=0,anchor=-1;float interval=-1;};
inline float phase_remaining(const Phase& p) {
    if(p.anchor<0||p.game_time<p.anchor||!std::isfinite(p.interval)||p.interval<=0)return -1;
    const float elapsed=static_cast<float>(p.game_time-p.anchor)/1000.0f;
    return p.interval-std::fmod(elapsed,p.interval);
}
// A sampled interval does not establish a scheduled deadline. No stopwatch is
// synthesized; phase remains unknown until a real scheduler deadline is read.
inline bool fresh(const Snapshot& s,const nav::Graph& map,std::uint64_t now) {
    return s.map&&spawns::same_map(*s.map,map)&&now>=s.updated&&now-s.updated<=300;
}
inline Category category(const Snapshot* s,int entity) {
    if(s)for(const auto& entry:s->entries)if(entry.entity==entity)return entry.category;
    return Category::unknown;
}
inline int special(const Snapshot* s,int entity) {
    if(s)for(const auto& entry:s->entries)if(entry.entity==entity)return entry.special;
    return 0;
}
inline bool nearby(Category c) {return c==Category::proximity||c==Category::proximity_and_view;}
inline bool view_only(Category c) {return c==Category::view;}
// Exempt/unknown categories do not independently establish proximity.
inline bool known_outside_nearby(Category c) {return c==Category::candidate||c==Category::view;}
struct Counts {
    unsigned total=0,candidates=0,in_view=0,proximity=0,exempt=0,unknown=0;
    void add(Category c) {
        ++total;
        if(c==Category::candidate)++candidates;
        else if(c==Category::view)++in_view;
        else if(c==Category::proximity||c==Category::proximity_and_view)++proximity;
        else if(c==Category::exempt)++exempt;
        else ++unknown;
    }
};
class Batch {
    std::shared_ptr<Snapshot> pending;
    int sequence=0,expected=0;
    std::array<bool,1024> seen{};
public:
    void clear() {pending.reset();sequence=expected=0;seen.fill(false);}
    bool begin(std::shared_ptr<const nav::Graph> map,const char* script,int seq,int count,float interval) {
        clear();
        if(!map||!spawns::map_name_matches(*map,script)||seq<=0||seq>1000000000||count<0||count>32||
            !std::isfinite(interval)||(interval!=-1&&(interval<=0||interval>120)))return false;
        pending=std::make_shared<Snapshot>();pending->map=std::move(map);pending->interval=interval;
        sequence=seq;expected=count;return true;
    }
    bool entry(int seq,int entity,int state,int special=0) {
        if(!pending||seq!=sequence||entity<0||entity>=1024||state<0||state>5||special<0||special>5||seen[entity]||pending->entries.size()>=static_cast<std::size_t>(expected)) {
            clear();return false;
        }
        seen[entity]=true;pending->entries.push_back({entity,static_cast<Category>(state),special});return true;
    }
    std::shared_ptr<const Snapshot> commit(const nav::Graph& map,int seq,int count,std::uint64_t now) {
        if(!pending||seq!=sequence||count!=expected||pending->entries.size()!=static_cast<std::size_t>(expected)||!spawns::same_map(map,*pending->map)) {clear();return {};}
        pending->updated=now;auto out=pending;clear();return out;
    }
};
std::shared_ptr<const Snapshot> snapshot();
Events events();
// A living zombie's entity was freed or reused without dying, seen by the native
// zombie reader (zombies::update): the despawn count. The helper's zombie_delete
// listener never fired, because vanilla deletes the zombie in the same frame as the
// notify, which kills the listener's thread with it (108 logged checks, 0 deletes).
void zombie_deleted(const std::shared_ptr<const nav::Graph>& map,int entity);
void clear();
}
