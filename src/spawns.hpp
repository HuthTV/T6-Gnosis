#pragma once
#include "navigation.hpp"

namespace plutonium::sdk::v1 {class iinterface;}
namespace spawns {
// Registry membership: normal=1, leaper=2, Denizen=4, ghost=8, Panzer=16, Templar=32, Brutus=64. Special chooser state is not decoded.
struct Site {nav::Vec3 origin;std::uint32_t id=0;bool active=false;unsigned types=1;};
// Rendering groups only exactly coincident finite origins. Keep raw registry
// records/counts intact; nearby authored positions are not equivalent sites.
inline std::vector<Site> drawing_sites(std::vector<Site> sites) {
    std::sort(sites.begin(),sites.end(),[](const Site& a,const Site& b) {
        if(a.origin.x!=b.origin.x)return a.origin.x<b.origin.x;
        if(a.origin.y!=b.origin.y)return a.origin.y<b.origin.y;
        return a.origin.z<b.origin.z;
    });
    std::vector<Site> grouped;grouped.reserve(sites.size());
    for(const auto& site:sites) {
        if(!grouped.empty()&&site.origin.x==grouped.back().origin.x&&site.origin.y==grouped.back().origin.y&&site.origin.z==grouped.back().origin.z) {
            grouped.back().types|=site.types;grouped.back().active=grouped.back().active||site.active;
        } else grouped.push_back(site);
    }
    return grouped;
}
inline std::string site_label(unsigned types) {
    std::string label;
    for(const auto& identity:{std::pair<unsigned,const char*>{1,"Zombie"},{2,"Leaper"},{4,"Denizen"},{8,"Ghost"},{16,"Panzer"},{32,"Templar"},{64,"Brutus"}})
        if(types&identity.first) {if(!label.empty())label+='\n';label+=identity.second;}
    return label;
}
inline unsigned visible_types(unsigned types,bool specials) {return specials?types:(types&1u);}
struct Snapshot {
    std::shared_ptr<const nav::Graph> map;
    std::vector<Site> sites;
    std::uint64_t updated=0;
    unsigned active_count=0;
};
// v1::iinterface is the pinned SDK's plutonium::sdk::iinterface alias.
bool startup(plutonium::sdk::v1::iinterface* api);
void update();
void clear();
std::shared_ptr<const Snapshot> snapshot();
const char* status();
}
