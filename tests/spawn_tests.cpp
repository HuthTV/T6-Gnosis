#include "spawns_batch.hpp"
#include <cstdio>
#include <limits>

namespace {
int checks=0,failures=0;
void check(bool good,const char* why) {++checks;if(!good) {++failures;std::printf("FAIL: %s\n",why);}}
std::shared_ptr<nav::Graph> graph() {
    auto g=std::make_shared<nav::Graph>();g->data=0x20000;g->base=0x30000;g->count=889;g->map="maps/mp/zm_nuked.d3dbsp";return g;
}
}
int main() {
    auto g=graph();spawns::Batch b;
    check(spawns::map_name_matches(*g,"zm_nuked"),"loaded Zombies filename matches script map name");
    check(!spawns::map_name_matches(*g,"nuked")&&!spawns::map_name_matches(*g,"zm_transit"),"wrong or abbreviated map rejected");
    auto malformed=*g;malformed.map="other/zm_nuked.d3dbsp";
    check(!spawns::map_name_matches(malformed,"zm_nuked"),"unexpected map path rejected");
    check(b.begin(g,"zm_nuked",1,2),"bounded current-map batch accepted");
    check(b.site(1,1,{20,30,40},0)&&b.site(1,0,{1,2,3},1),"active and inactive sites accepted out of order");
    auto s=b.commit(g,1,2,500);
    check(s&&s->sites.size()==2&&s->active_count==1&&s->sites[0].id==0&&s->sites[0].active&&!s->sites[1].active&&s->updated==500,"complete snapshot is coherent and timestamped");
    check(!b.commit(g,1,2,600),"committed batch cannot be published twice");
    check(!b.begin(g,"zm_nuked",1,2),"old sequence cannot revive historical data");
    check(b.begin(g,"zm_nuked",2,2)&&b.site(2,0,{1,2,3},1)&&!b.commit(g,2,2,700),"partial batch is never published");
    check(b.begin(g,"zm_nuked",3,1)&&b.site(3,0,{1,2,3},1)&&!b.site(3,0,{9,9,9},0)&&!b.commit(g,3,1,700),"duplicate site invalidates entire batch");
    check(b.begin(g,"zm_nuked",4,1)&&!b.site(5,0,{1,2,3},1)&&!b.commit(g,4,1,700),"wrong site sequence invalidates batch");
    check(b.begin(g,"zm_nuked",5,1)&&!b.site(5,-1,{1,2,3},1),"negative ID rejected");
    check(b.begin(g,"zm_nuked",6,1)&&!b.site(6,1,{1,2,3},1),"ID must be below declared count");
    check(b.begin(g,"zm_nuked",7,1)&&!b.site(7,0,{1,2,3},2),"unknown active state rejected");
    check(b.begin(g,"zm_nuked",8,1)&&!b.site(8,0,{1,2,3},-1),"negative active state rejected");
    const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
    check(b.begin(g,"zm_nuked",9,1)&&!b.site(9,0,{nan,0,0},1),"NaN origin rejected");
    check(b.begin(g,"zm_nuked",10,1)&&!b.site(10,0,{0,inf,0},1),"infinite origin rejected");
    check(b.begin(g,"zm_nuked",11,1)&&!b.site(11,0,{0,0,-1000001},1),"coordinate bounds enforced");
    check(b.begin(g,"zm_nuked",12,1)&&b.site(12,0,{-1000000,1000000,0},0),"finite boundary coordinates accepted");
    auto refreshed=std::make_shared<nav::Graph>(*g);
    check(static_cast<bool>(b.commit(refreshed,12,1,800)),"same map identity survives ordinary graph refresh");
    auto changed=std::make_shared<nav::Graph>(*g);changed->base+=0x100;
    check(b.begin(g,"zm_nuked",13,1)&&b.site(13,0,{1,2,3},1)&&!b.commit(changed,13,1,900),"changed graph allocation rejected at commit");
    changed=std::make_shared<nav::Graph>(*g);changed->map="maps/mp/zm_transit.d3dbsp";
    check(b.begin(g,"zm_nuked",14,0)&&!b.commit(changed,14,0,900),"map switch rejects even empty pending batch");
    check(b.begin(g,"zm_nuked",15,0)&&!b.commit(g,16,0,900),"commit sequence mismatch rejected");
    check(b.begin(g,"zm_nuked",16,1)&&b.site(16,0,{1,2,3},1)&&!b.commit(g,16,0,900),"commit count mismatch rejected");
    check(!b.begin(g,"zm_nuked",0,0)&&!b.begin(g,"zm_nuked",1000000001,0),"sequence limits enforced");
    check(!b.begin(g,"zm_nuked",17,-1)&&!b.begin(g,"zm_nuked",17,4097),"site count limits enforced");
    check(!b.begin({},"zm_nuked",17,0)&&!b.begin(g,"zm_transit",17,0),"missing or wrong map rejected before publication");
    check(b.begin(g,"zm_nuked",18,0),"empty batch accepted");
    s=b.commit(g,18,0,1000);
    check(s&&s->sites.empty()&&s->active_count==0,"complete empty snapshot clears sites");
    check(b.begin(g,"zm_nuked",19,4096)&&b.site(19,4095,{1,2,3},1),"maximum bounded count and final valid ID accepted");
    b.clear();check(!b.commit(g,19,4096,1000),"session clear discards staging snapshot");
    check(b.begin(g,"zm_nuked",1,0)&&static_cast<bool>(b.commit(g,1,0,1100)),"fresh session can restart sequence safely");
    auto old_snapshot=s;b.begin(g,"zm_nuked",2,1);b.site(2,0,{9,8,7},1);auto latest=b.commit(g,2,1,1200);
    check(old_snapshot&&old_snapshot->sites.empty()&&latest&&latest->sites.size()==1,"published snapshots own their values across later commits");
    std::printf("Spawn publication checks: %s (%d checks, %d failures)\n",failures?"FAIL":"PASS",checks,failures);return failures?1:0;
}
