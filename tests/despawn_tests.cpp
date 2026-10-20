#include "despawn.hpp"
#include "despawn_schedule.hpp"
#include "despawn_provider.hpp"
#include <cstdio>
#include <limits>
int main() {
    int failures=0,checks=0;
    const auto check=[&](bool good,const char* why) {++checks;if(!good) {++failures;std::printf("FAIL: %s\n",why);}};
    {using namespace despawn::shadow;Sample a,b;auto map=std::make_shared<nav::Graph>();map->map="maps/mp/zm_buried.d3dbsp";a.context={map,1,100,true};b.context=a.context;b.origin=Origin::native_independent;a.tracker={true,108,0.4f};b.tracker=a.tracker;check(compare(a,b).tracker==Verdict::match,"Independent aligned tracker samples match");b.tracker.deadline=109;check(compare(a,b).tracker==Verdict::mismatch,"Aligned deadline disagreement detected");b.context.tick=101;check(compare(a,b).tracker==Verdict::inconclusive,"Asynchronous observations do not create false mismatch");b.context=a.context;b.origin=Origin::observer_assisted;check(compare(a,b).tracker==Verdict::inconclusive,"Same provider cannot validate itself");b=native_unavailable(a.context);check(compare(a,b).tracker==Verdict::unavailable,"Unimplemented native reader cannot count as match");b.tracker=a.tracker;b.context.generation=2;check(compare(a,b).tracker==Verdict::inconclusive,"Session restart separates samples");b.context=a.context;a.actors_available=b.actors_available=true;a.actors={{3,42,true,despawn::Category::view,0}};b.actors=a.actors;check(compare(a,b).actors==Verdict::match,"Aligned actor lifetime and category match");b.actors[0].lifetime=43;check(compare(a,b).actors==Verdict::inconclusive,"Entity reuse cannot masquerade as category mismatch");b.actors=a.actors;b.actors[0].category=despawn::Category::candidate;check(compare(a,b).actors==Verdict::mismatch,"Same lifetime category difference detected");b.actors.push_back(b.actors[0]);check(compare(a,b).actors==Verdict::inconclusive,"Duplicate actor identities reject comparison");a.deletion_requests_available=b.deletion_requests_available=true;check(compare(a,b).deletion_requests==Verdict::inconclusive,"Aggregate request totals do not prove matching removals");auto source=std::make_shared<despawn::Snapshot>();source->map=map;source->entries={{3,despawn::Category::view,0}};despawn::schedule::Sample timer;timer.map=map;timer.clock=100;timer.deadline=108;timer.remaining=0.4f;timer.interval=0.4f;auto adapted=observer_sample(source,{},timer);check(adapted.tracker.available&&!adapted.context.aligned&&!adapted.actors[0].lifetime_known&&!adapted.replacements_available,"Observer adapter never invents alignment lifetime or replacements");}
    check(despawn::schedule::remaining_ticks(108,100)==8,"Buried eight-tick deadline");
    check(despawn::schedule::remaining_ticks(300,100)==200,"Die Rise 200-tick deadline" );
    check(despawn::schedule::remaining_ticks(3,0xFFFFFE)==5,"24-bit VM clock wrap" );
    check(despawn::schedule::remaining_ticks(99,100)==0xFFFFFF,"Past deadline is rejected by interval bounds" );
    check(despawn::schedule::valid_container(18,2,39),"Live scheduler struct size field is not child count");
    check(despawn::schedule::valid_container(21,39,39),"Array child count remains validated" );
    check(!despawn::schedule::valid_container(21,2,39),"Array size mismatch rejected" );
    check(!despawn::schedule::valid_container(16,2,39),"Thread is not a registry container" );
    despawn::schedule::PassResults results;
    results.observe(100,108,0,4);check(!results.available,"Initial deadline does not invent completed pass" );
    results.observe(108,116,0,3);results.observe(111,116,2,0);check(results.available&&results.despawned==2&&results.prevented==3,"Delayed deletion window and boundary visibility" );
    results.observe(116,124,2,0);check(!results.available,"Next cycle immediately hides prior notification");results.observe(119,124,2,0);check(!results.available&&results.despawned==0&&results.prevented==0,"Next zero-result pass replaces positive result" );
    results.observe(120,124,0,0);check(!results.available,"Event counter reset clears pass history" );
    despawn::schedule::PassResults only;only.observe(100,108,0,0);only.observe(108,116,0,0);only.observe(111,116,0,0);only.observe(112,116,4,0);check(only.available&&only.despawned==4&&only.prevented==0,"Late despawn-only notifications remain visible until next cycle");only.observe(116,124,4,0);check(!only.available,"Next cycle clears late despawn-only result");
    despawn::schedule::PassResults early;early.observe(100,200,0,0);early.observe(110,250,0,0);early.observe(113,250,1,0);check(!early.available,"Changing earliest actor before its deadline does not invent a pass");
    despawn::Phase phase;phase.game_time=1500;phase.anchor=1000;phase.interval=10;
    check(std::abs(despawn::phase_remaining(phase)-9.5f)<0.001f,"phase uses elapsed script time");
    phase.game_time=500;check(despawn::phase_remaining(phase)<0,"script-clock reversal suppresses stale anchor");
    phase.game_time=11500;check(std::abs(despawn::phase_remaining(phase)-9.5f)<0.001f,"phase wraps configured interval");
    phase.anchor=-1;check(despawn::phase_remaining(phase)<0,"unknown anchor suppresses estimate");
    auto map=std::make_shared<nav::Graph>();map->map="maps/mp/zm_highrise.d3dbsp";map->base=0x10000;map->data=0x20000;map->count=50;
    despawn::Batch batch;
    check(batch.begin(map,"zm_highrise",1,3,10),"valid transaction");
    check(batch.entry(1,24,1)&&batch.entry(1,25,2)&&batch.entry(1,26,4),"candidates, nearby and exempt stored independently");
    auto snapshot=batch.commit(*map,1,3,1000);
    check(snapshot&&snapshot->entries.size()==3,"complete batch committed");
    check(snapshot&&despawn::fresh(*snapshot,*map,1300),"freshness boundary accepted");
    check(snapshot&&!despawn::fresh(*snapshot,*map,1301),"stale classifications rejected");
    check(snapshot&&!despawn::fresh(*snapshot,*map,999),"clock reversal rejected");
    auto changed=*map;changed.data++;
    check(snapshot&&!despawn::fresh(*snapshot,changed,1100),"map allocation change invalidates snapshot");
    check(despawn::category(snapshot.get(),99)==despawn::Category::unknown,"unobserved living actor remains unknown");
    despawn::Counts counts;for(auto c:{despawn::Category::candidate,despawn::Category::proximity,despawn::Category::view,despawn::Category::proximity_and_view,despawn::Category::exempt,despawn::Category::unknown})counts.add(c);
    check(counts.total==6&&counts.candidates==1&&counts.in_view==1&&counts.proximity==2&&counts.exempt==1&&counts.unknown==1,"all count categories partition total");
    despawn::Counts overlap;overlap.add(despawn::Category::proximity_and_view);
    check(overlap.total==1&&overlap.proximity==1&&overlap.in_view==0,"overlapping protection displays only as Nearby");
    check(despawn::nearby(despawn::Category::proximity_and_view)&&!despawn::view_only(despawn::Category::proximity_and_view)&&despawn::view_only(despawn::Category::view),"eye applies only to view without nearby protection");
    check(despawn::known_outside_nearby(despawn::Category::candidate)&&despawn::known_outside_nearby(despawn::Category::view),"outside-nearby filter includes candidates and view-only protection");
    check(!despawn::known_outside_nearby(despawn::Category::proximity)&&!despawn::known_outside_nearby(despawn::Category::proximity_and_view)&&!despawn::known_outside_nearby(despawn::Category::exempt)&&!despawn::known_outside_nearby(despawn::Category::unknown),"nearby and unestablished proximity never appear as known outside range");
    check(batch.begin(map,"zm_highrise",9,1,10)&&batch.entry(9,24,4,4),"leaper identity accepts exempt cleanup state");
    auto leaper=batch.commit(*map,9,1,1100);
    check(leaper&&despawn::special(leaper.get(),24)==4&&despawn::category(leaper.get(),24)==despawn::Category::exempt,"special identity is independent from cleanup eligibility");
    check(batch.begin(map,"zm_highrise",10,1,10)&&!batch.entry(10,24,4,6),"unknown special IDs reject publication");
    auto transit=std::make_shared<nav::Graph>(*map);transit->map="maps/mp/zm_transit.d3dbsp";
    check(batch.begin(transit,"zm_transit",11,1,10)&&batch.entry(11,24,4,5),"Avogadro identity accepts exempt ordinary-cleanup state");
    auto avogadro=batch.commit(*transit,11,1,1100);
    check(avogadro&&despawn::special(avogadro.get(),24)==5&&despawn::category(avogadro.get(),24)==despawn::Category::exempt,"Avogadro special shape does not imply ordinary cleanup eligibility");
    check(batch.begin(map,"zm_highrise",2,2,0.4f)&&batch.entry(2,24,3),"live fractional interval accepted without inventing phase");
    check(!batch.entry(2,24,2)&&!batch.commit(*map,2,2,1100),"duplicate ID aborts batch");
    check(batch.begin(map,"zm_highrise",3,1,-1)&&batch.entry(3,24,0),"unknown interval and classification accepted");
    check(!batch.commit(changed,3,1,1200),"map changed during transaction rejected");
    check(!batch.begin(map,"zm_tomb",4,0,10),"wrong map rejected");
    check(!batch.begin(map,"zm_highrise",4,33,10),"actor bound enforced");
    check(!batch.begin(map,"zm_highrise",4,0,std::numeric_limits<float>::quiet_NaN()),"invalid interval rejected");
    check(batch.begin(map,"zm_highrise",4,1,10)&&!batch.entry(5,24,1),"wrong sequence aborts batch");
    check(batch.begin(map,"zm_highrise",5,1,10)&&!batch.entry(5,24,6),"invalid category rejected");
    check(batch.begin(map,"zm_highrise",6,1,10)&&!batch.entry(6,1024,1),"invalid entity rejected");
    check(batch.begin(map,"zm_highrise",7,1,10)&&!batch.commit(*map,7,1,1500),"incomplete snapshot never published");
    check(batch.begin(map,"zm_highrise",8,0,10),"empty living population accepted");
    batch.clear();check(!batch.commit(*map,8,0,1600),"session reset removes pending publication");
    std::printf("Despawn telemetry checks: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
