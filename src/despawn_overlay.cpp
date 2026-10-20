#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdio>
#include "despawn_overlay.hpp"
#include "fonts.hpp"
#include "zombies.hpp"
namespace despawn::ui {
Settings settings;
namespace {
bool& enabled=settings.enabled;
bool& show_hud=settings.show_hud;
bool& show_phase=settings.show_phase;
bool& outside_nearby_only=settings.outside_nearby_only;
schedule::PassResults pass_results;
std::shared_ptr<const nav::Graph> results_map;
ImU32 color(Category c) {
    if(c==Category::candidate||c==Category::view)return IM_COL32(255,175,45,255);
    if(nearby(c))return IM_COL32(255,15,25,255);
    if(c==Category::exempt)return IM_COL32(190,135,255,255);
    return IM_COL32(180,185,195,255);
}
}
View capture(const std::shared_ptr<const nav::Graph>& map) {
    View view;view.now=GetTickCount64();view.observer=despawn::snapshot();view.observed=despawn::events();view.map=map;
    nav::Camera camera{};
    if(!map||!nav::read_camera(camera,*map)) {view.observed={};view.observer.reset();return view;}
    const auto estimate=schedule::sample();
    if(!estimate.map||spawns::same_map(*estimate.map,*map))view.scheduled=estimate;
    view.observer_valid=view.observer&&fresh(*view.observer,*map,view.now);
    view.distance_tracker=view.observer&&view.observer->map&&spawns::same_map(*view.observer->map,*map)&&view.observer->interval>0;
    view.unsupported=view.observer_valid&&view.observer->interval<=0;
    if(view.observed.map&&!spawns::same_map(*view.observed.map,*map))view.observed={};
    const auto living=zombies::snapshot();
    view.living_valid=living&&living->map&&spawns::same_map(*living->map,*map)&&view.now>=living->updated&&view.now-living->updated<=200;
    if(!view.living_valid)return view;
    for(const auto& marker:living->markers) {
        nav::Vec3 origin{};if(!zombies::still_living(marker,origin))continue;
        view.counts.add(category(view.observer_valid?view.observer.get():nullptr,marker.number));
    }
    if(!results_map||!spawns::same_map(*results_map,*map)) {pass_results={};results_map=map;}
    if(view.scheduled.remaining>=0&&view.observer_valid)
        pass_results.observe(view.scheduled.clock,view.scheduled.deadline,view.observed.notifications,view.counts.in_view);
    view.results=pass_results;
    return view;
}
void menu(const View& view,bool wallhack) {
    if(view.unsupported) {
        ImGui::BeginDisabled();bool off=false;ImGui::Checkbox("Despawn tracker",&off);ImGui::EndDisabled();
        ImGui::SameLine();ImGui::TextDisabled("(no despawn cleanup on this map)");
        return;
    }
    ImGui::Checkbox("Despawn tracker",&enabled);
    if(ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();ImGui::PushTextWrapPos(ImGui::GetFontSize()*30);
        ImGui::TextUnformatted("Zombie counts, the time to the next cleanup check and its result. With the wallhack, arrows are coloured: red nearby (kept), yellow out of range (deleted at the check unless in view; an eye marks one in view), purple exempt, gray unknown.");
        ImGui::PopTextWrapPos();ImGui::EndTooltip();
    }
    if(!enabled)return;
    ImGui::Indent();
    ImGui::Checkbox("Counts",&show_hud);ImGui::SameLine();ImGui::Checkbox("Timer",&show_phase);
    ImGui::BeginDisabled(!wallhack);
    ImGui::SameLine();ImGui::Checkbox("Only out-of-range arrows",&outside_nearby_only);
    ImGui::EndDisabled();
    ImGui::Unindent();
}
// Top-left corner of the despawn HUD in 1080p units. Origins and Mob of the Dead
// put it under the game's parts display (user placement from 1440p screenshots:
// Origins icons end near y 90, Mob plane parts near y 109); every other map uses the
// corner.
ImVec2 anchor(const std::shared_ptr<const nav::Graph>& map) {
    if(map&&map->map=="maps/mp/zm_tomb.d3dbsp")return {10,94};
    if(map&&map->map=="maps/mp/zm_prison.d3dbsp")return {10,113};
    return {10,6};
}
void hud(const View& view,float,float height) {
    // Only in a loaded map: fresh living-zombie data (not in the main menu).
    if(!enabled||!show_hud||!view.living_valid||!view.distance_tracker)return;
    const float scale=height/1080.0f,font_size=22.0f*scale;
    const auto origin=anchor(view.map);
    auto* list=ImGui::GetBackgroundDrawList();
    const auto text=[&](const char* value,float row,ImU32 c) {
        const ImVec2 at(origin.x*scale,(origin.y+row*27)*scale);
        list->AddText(fonts::get(font_size),font_size,ImVec2(at.x+scale,at.y+scale),IM_COL32(0,0,0,255),value);
        list->AddText(fonts::get(font_size),font_size,at,c,value);
    };
    char line[128]{};
    std::snprintf(line,sizeof(line),"Zombies %u | Nearby %u | In view %u",view.counts.total,view.counts.proximity,view.counts.in_view);
    if(!view.distance_tracker)std::snprintf(line,sizeof(line),"Zombies %u",view.counts.total);
    text(line,0,IM_COL32(240,240,245,255));

    if(view.distance_tracker&&view.results.available) {
        std::snprintf(line,sizeof(line),"Despawns %u | Prevented %u",view.results.despawned,view.results.prevented);
        text(line,2.7f,IM_COL32(115,220,245,255));
    }
    if(show_phase&&view.distance_tracker) {
        const float remaining=view.scheduled.remaining;
        if(remaining<0)std::snprintf(line,sizeof(line),"Despawn");
        else std::snprintf(line,sizeof(line),"Despawn %.2f s",remaining);
        text(line,1,IM_COL32(225,205,145,255));
        char width_text[64]{};
        std::snprintf(width_text,sizeof(width_text),"Despawn %.2f s",std::max(view.scheduled.interval,0.0f));
        const float bar_width=fonts::get(font_size)->CalcTextSizeA(font_size,10000.0f,0.0f,width_text).x;
        // Bar below the timer row (57 and 65 units under the first row's top).
        const ImVec2 start(origin.x*scale,(origin.y+57)*scale),end(origin.x*scale+bar_width,(origin.y+65)*scale);
        list->AddRectFilled(start,end,IM_COL32(30,30,35,210));
        if(remaining>=0&&view.scheduled.interval>0) {
            const float fraction=std::clamp(remaining/view.scheduled.interval,0.0f,1.0f);
            list->AddRectFilled(start,ImVec2(start.x+(end.x-start.x)*fraction,end.y),IM_COL32(225,185,90,255));
        }
    }
}
ImU32 arrow_color(const View& view,int entity) {
    return enabled&&view.distance_tracker?color(category(view.observer_valid?view.observer.get():nullptr,entity)):IM_COL32(255,15,25,255);
}
bool arrow_eye(const View& view,int entity) {
    return enabled&&view.distance_tracker&&view_only(category(view.observer_valid?view.observer.get():nullptr,entity));
}
bool arrow_visible(const View& view,int entity) {
    return !enabled||!view.distance_tracker||!outside_nearby_only||known_outside_nearby(category(view.observer_valid?view.observer.get():nullptr,entity));
}
}
