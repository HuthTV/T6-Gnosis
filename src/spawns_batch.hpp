#pragma once
#include "spawns.hpp"
#include <string_view>

namespace spawns {
inline bool same_map(const nav::Graph& a,const nav::Graph& b) {
    return a.data==b.data&&a.base==b.base&&a.count==b.count&&a.map==b.map;
}
inline bool map_name_matches(const nav::Graph& g,std::string_view name) {
    constexpr std::string_view prefix="maps/mp/",suffix=".d3dbsp";
    const std::string_view bsp=g.map;
    return name.size()>3&&name.size()<=31&&name.substr(0,3)=="zm_"&&
        bsp.size()==prefix.size()+name.size()+suffix.size()&&
        bsp.substr(0,prefix.size())==prefix&&
        bsp.substr(prefix.size(),name.size())==name&&
        bsp.substr(prefix.size()+name.size())==suffix;
}
inline bool valid_origin(nav::Vec3 origin) {
    return nav::finite(origin)&&std::abs(origin.x)<=1000000.f&&
        std::abs(origin.y)<=1000000.f&&std::abs(origin.z)<=1000000.f;
}
// Pure transaction state, also used by the production callbacks. No VM pointers
// or Windows calls survive an argument read; only copied sites enter this batch.
class Batch {
    std::shared_ptr<Snapshot> staging_;
    std::shared_ptr<const nav::Graph> last_map_;
    std::vector<bool> seen_;
    unsigned received_=0;
    int sequence_=0,last_sequence_=0;
public:
    void abort() {staging_.reset();seen_.clear();received_=0;sequence_=0;}
    void clear() {abort();last_map_.reset();last_sequence_=0;}
    bool begin(std::shared_ptr<const nav::Graph> map,std::string_view name,int sequence,int count) {
        abort();
        if(!map||!map->count||!map_name_matches(*map,name)||sequence<=0||sequence>1000000000||count<0||count>4096)return false;
        if(last_map_&&!same_map(*map,*last_map_)) {last_map_.reset();last_sequence_=0;}
        if(sequence<=last_sequence_)return false;
        auto next=std::make_shared<Snapshot>();next->map=std::move(map);
        next->sites.resize(static_cast<std::size_t>(count));
        seen_.assign(static_cast<std::size_t>(count),false);
        staging_=std::move(next);sequence_=sequence;return true;
    }
    bool site(int sequence,int id,nav::Vec3 origin,int active) {
        if(!staging_||sequence!=sequence_||id<0||static_cast<std::size_t>(id)>=staging_->sites.size()||
           (active!=0&&active!=1)||!valid_origin(origin)) {abort();return false;}
        const auto index=static_cast<std::size_t>(id);
        if(seen_[index]) {abort();return false;}
        seen_[index]=true;++received_;
        staging_->sites[index]={origin,static_cast<std::uint32_t>(id),active==1};
        if(active)++staging_->active_count;
        return true;
    }
    std::shared_ptr<const Snapshot> commit(const std::shared_ptr<const nav::Graph>& map,int sequence,int count,std::uint64_t now) {
        if(!staging_||!map||sequence!=sequence_||count<0||static_cast<std::size_t>(count)!=staging_->sites.size()||
           received_!=staging_->sites.size()||!same_map(*map,*staging_->map)) {abort();return {};}
        staging_->updated=now;last_map_=staging_->map;last_sequence_=sequence_;
        std::shared_ptr<const Snapshot> next=std::move(staging_);abort();return next;
    }
};
}
