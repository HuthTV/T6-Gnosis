#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <MinHook.h>
#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <chrono>
#include <future>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include "overlay.hpp"
#include "navigation.hpp"
#include "zombies.hpp"
#include "triggers.hpp"
#include "spawns.hpp"
#include "practice.hpp"
#include "despawn_overlay.hpp"
#include "fonts.hpp"
#include "playable_area.hpp"
#include "version.hpp"
#include "staff_aid.hpp"
#include "mob_aid.hpp"
#include "world_collision.hpp"
#include "gsc_args.hpp"
#include "collision_gpu.hpp"
#include "frame_trace.hpp"

#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib,"dwmapi.lib")
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);

namespace overlay {
namespace {
using Present=HRESULT(__stdcall*)(IDXGISwapChain*,UINT,UINT);
using Resize=HRESULT(__stdcall*)(IDXGISwapChain*,UINT,UINT,UINT,DXGI_FORMAT,UINT);
Present next_present=nullptr;Resize next_resize=nullptr;
void* present_address=nullptr;void* resize_address=nullptr;
IDXGISwapChain* chain=nullptr;
ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;
ID3D11RenderTargetView* target=nullptr;
ImGuiContext* gui=nullptr;
HWND window=nullptr;
WNDPROC previous_proc=nullptr;
std::recursive_mutex guard;
std::atomic<bool> stopping{false};
std::atomic<bool> input_active{false},moving_window{false};
// Defaults (user requests 2026-10-13): every feature off except the stamina bar,
// speedometer and jump stats.
bool visible=true,nodes=false,zombie_markers=false,links=false,labels=false,arrows=false;
bool spawn_sites=false;
bool trigger_positions=false,trigger_labels=false,playable_areas=false,player_bounds=false;
bool wind_aid=true,fire_aid=true,wind_details=false,fire_details=false;
bool mob_skulls=false,mob_boxes=false;  // Mob of the Dead blue-skull aid and Afterlife shock boxes
bool world_collision=false;
float collision_range=600,collision_opacity=0.85f;
unsigned collision_drawn=0;
// Strength of everything drawn behind the pause menu (user choice 2026-10-13).
constexpr float paused_strength=0.5f;
bool frame_paused=false;
// GPU collision pass: built during draw(), submitted at Present before ImGui.
collision_gpu::Batch collision_batch;bool collision_gpu_pass=false,collision_gpu_failed=false;
bool zombie_boxes=false;unsigned zombie_boxes_drawn=0;
// Recent server boxes per zombie entity number. The scene shows client-interpolated
// positions that trail the server entity, so a running zombie's occlusion box spans
// its last trail_ms of positions instead of only the newest one.
constexpr std::uint64_t trail_ms=150;
struct Trail {std::uint32_t actor=0;unsigned next=0;std::uint64_t time[32]{};nav::Vec3 lo[32]{},hi[32]{};};
std::unordered_map<std::uint16_t,Trail> trails;
// Drawing under the HUD: injected after the scene composite and tested against the
// scene depth (frame_trace). scene_near converts reversed depth to distance.
constexpr float scene_near=4;
// The last injection drew the NAV graph on the GPU (under zombies).
bool nav_injected=false;
// The last injection drew the world overlays (draw()'s world part) under the weapon
// and HUD; in_injection is set while it does. world_counts carries that drawing's
// counts to the F8 menu, which Present builds without the world part.
bool world_injected=false,in_injection=false;
struct WorldCounts {unsigned spawns=0,triggers=0,overlaps=0;} world_counts;
// Present's despawn view, reused by the injection (capture() also advances the
// despawn pass counter, so it runs once per frame).
despawn::ui::View last_despawn_view;
unsigned backbuffer_width=0,backbuffer_height=0;
float trigger_range=2000;
bool spawn_labels=false;
bool special_spawn_sites=false;
bool show_stamina=true,show_speedometer=true,show_jump_stats=true;
bool active_spawns_only=false;
float spawn_range=2000;
bool menu_open=false,clip_saved=false;
float menu_x=-1,menu_y=-1,menu_w=-1,menu_h=-1;  // saved menu placement (T6Gnosis.cfg)
RECT previous_clip{};
float wheel=0;
float range=800;
// One radius per feature (user request 2026-10-13): the vertical extent is a fixed
// share of the horizontal radius.
constexpr float vertical_share=1.0f/3;
float vertical(float radius) {return radius*vertical_share;}
std::array<bool,256> down{};
// Quick controls: one key per major top-level toggle (user request 2026-10-20), in
// menu order. setting is the cfg key name (keep its spelling: saved binds use it).
struct QuickBinding {const char* label;const char* setting;bool* value;int key=0;};
std::array<QuickBinding,18> quick{{
    {"Overlay master switch","key_overlay",&visible},{"Stamina bar","key_stamina_bar",&show_stamina},
    {"Speedometer","key_speedometer",&show_speedometer},{"Jump stats","key_jump_stats",&show_jump_stats},
    {"Wallhack","key_wallhack",&zombie_markers},{"Despawn tracker","key_despawn_tracker",&despawn::ui::settings.enabled},
    {"Spawn points","key_spawn_points",&spawn_sites},{"World collision","key_world_collision",&world_collision},
    {"Zombie hitboxes","key_zombie_hitboxes",&zombie_boxes},
    {"bbox","key_bbox",&player_bounds},{"Trigger geometry","key_triggers",&trigger_positions},
    {"Playable area","key_playable_area",&playable_areas},{"NAV nodes","key_nav_nodes",&nodes},
    {"NAV links","key_nav_links",&links},{"Origins wind aid","key_wind_aid",&wind_aid},
    {"Origins fire aid","key_fire_aid",&fire_aid},{"Mob blue skull aid","key_mob_skulls",&mob_skulls},
    {"Mob shock boxes","key_mob_shock_boxes",&mob_boxes}}};
int binding_capture=-1;
// Saved settings (user request 2026-10-13): T6Gnosis.cfg next to the DLL, one
// key=value per line. The keys are the file format, so keep their spelling when
// renaming variables. Unknown keys are ignored; missing keys keep their defaults.
struct BoolSetting {const char* key;bool* value;bool fallback=false;};
struct FloatSetting {const char* key;float* value;float lo,hi;float fallback=0;};
BoolSetting bool_settings[]={
    {"overlay",&visible},{"wallhack",&zombie_markers},
    {"despawn_tracker",&despawn::ui::settings.enabled},{"despawn_counts",&despawn::ui::settings.show_hud},
    {"despawn_timer",&despawn::ui::settings.show_phase},{"despawn_out_of_range_only",&despawn::ui::settings.outside_nearby_only},
    {"stamina_bar",&show_stamina},{"speedometer",&show_speedometer},{"jump_stats",&show_jump_stats},
    {"wind_aid",&wind_aid},{"wind_details",&wind_details},{"fire_aid",&fire_aid},{"fire_details",&fire_details},
    {"mob_skulls",&mob_skulls},{"mob_shock_boxes",&mob_boxes},
    {"spawn_points",&spawn_sites},{"spawn_labels",&spawn_labels},{"special_spawns",&special_spawn_sites},{"active_spawns_only",&active_spawns_only},
    {"world_collision",&world_collision},
    {"zombie_collision_boxes",&zombie_boxes},
    
    {"player_bounds",&player_bounds},
    {"triggers",&trigger_positions},{"trigger_labels",&trigger_labels},{"playable_area",&playable_areas},
    {"nav_nodes",&nodes},{"nav_links",&links},{"nav_ids",&labels},{"nav_directions",&arrows},
};
FloatSetting float_settings[]={
    {"spawn_radius",&spawn_range,400,8000},{"collision_radius",&collision_range,100,3000},
    {"collision_opacity",&collision_opacity,0.2f,1},
    {"trigger_radius",&trigger_range,400,8000},{"nav_radius",&range,400,8000},
    // Menu window placement, -1 until first saved (then the default placement is used).
    {"menu_x",&menu_x,-1,16384},{"menu_y",&menu_y,-1,16384},{"menu_w",&menu_w,-1,16384},{"menu_h",&menu_h,-1,16384},
};
bool settings_loaded=false,reset_window=false;
std::string saved_settings;
std::uint64_t settings_checked=0;
// Path of a file beside the DLL; empty when the module path is unavailable.
std::string beside_dll(const char* name) {
    HMODULE module=nullptr;char path[MAX_PATH]{};
    if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&beside_dll),&module)||!GetModuleFileNameA(module,path,MAX_PATH))return {};
    char* slash=std::strrchr(path,'\\');if(!slash)return {};
    slash[1]=0;return std::string(path)+name;
}
std::string settings_text() {
    std::string text="# T6 Gnosis settings, saved by the F8 menu. Delete this file or press Reset to restore defaults.\n";
    char line[96]{};
    for(const auto& s:bool_settings) {std::snprintf(line,sizeof(line),"%s=%d\n",s.key,*s.value?1:0);text+=line;}
    for(const auto& s:float_settings) {std::snprintf(line,sizeof(line),"%s=%g\n",s.key,*s.value);text+=line;}
    for(std::size_t i=0;i<quick.size();++i) {std::snprintf(line,sizeof(line),"%s=%d\n",quick[i].setting,quick[i].key);text+=line;}
    return text;
}
void load_settings() {
    settings_loaded=true;
    for(auto& s:bool_settings)s.fallback=*s.value;
    for(auto& s:float_settings)s.fallback=*s.value;
    const auto path=beside_dll("T6Gnosis.cfg");
    FILE* file=nullptr;
    if(!path.empty()&&!fopen_s(&file,path.c_str(),"rb")&&file) {
        char line[160]{};
        while(std::fgets(line,sizeof(line),file)) {
            char* equals=std::strchr(line,'=');if(line[0]=='#'||!equals)continue;
            *equals=0;const char* value=equals+1;
            for(auto& s:bool_settings)if(!std::strcmp(line,s.key))*s.value=std::atoi(value)!=0;
            for(auto& s:float_settings)if(!std::strcmp(line,s.key)) {
                const float parsed=std::strtof(value,nullptr);
                if(std::isfinite(parsed))*s.value=std::clamp(parsed,s.lo,s.hi);
            }
            for(std::size_t i=0;i<quick.size();++i)if(!std::strcmp(line,quick[i].setting)) {
                const int key=std::atoi(value);
                quick[i].key=key>0&&key<256&&key!=VK_F8&&key!=VK_F9?key:0;
            }
        }
        std::fclose(file);
    }
    saved_settings=settings_text();
}
// Writes the file only when a setting changed; replaces it whole so a crash mid-write
// cannot leave a truncated file.
void save_settings() {
    if(!settings_loaded)return;
    auto text=settings_text();if(text==saved_settings)return;
    const auto path=beside_dll("T6Gnosis.cfg");if(path.empty())return;
    const auto temporary=path+".tmp";
    FILE* file=nullptr;if(fopen_s(&file,temporary.c_str(),"wb")||!file)return;
    const bool written=std::fwrite(text.data(),1,text.size(),file)==text.size();
    if(std::fclose(file)!=0||!written||!MoveFileExA(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING)) {DeleteFileA(temporary.c_str());return;}
    saved_settings=std::move(text);
}
void reset_settings() {
    for(auto& s:bool_settings)*s.value=s.fallback;
    for(auto& s:float_settings)*s.value=s.fallback;
    for(auto& binding:quick)binding.key=0;
    binding_capture=-1;reset_window=true;
}
void key_label(int key,char* out,int size) {
    if(!key) {std::snprintf(out,size," ");return;}
    const auto scan=MapVirtualKeyW(static_cast<UINT>(key),MAPVK_VK_TO_VSC_EX);
    if(!GetKeyNameTextA(static_cast<LONG>((scan&255)<<16)|((scan&0xFF00)?(1<<24):0),out,size))std::snprintf(out,size,"Key %d",key);
}
ULONGLONG last_frame=0;
struct FrameInfo {
    std::shared_ptr<const nav::Graph> graph;
    nav::Camera camera{};
    bool camera_valid=false;
    unsigned points=0,edges=0,zombie_count=0;
    std::shared_ptr<const spawns::Snapshot> spawn_snapshot;
    bool spawn_valid=false;
    unsigned spawn_count=0;
    std::shared_ptr<const triggers::Snapshot> trigger_snapshot;
    unsigned trigger_count=0,player_overlaps=0;triggers::PlayerBox player{};bool player_valid=false;
    despawn::ui::View despawn_view;
};
template<class T> void release(T*& p) {if(p){p->Release();p=nullptr;}}
bool pressed(int key) {
    const bool now=(GetAsyncKeyState(key)&0x8000)!=0;
    const bool edge=now&&!down[static_cast<std::size_t>(key)];down[static_cast<std::size_t>(key)]=now;return edge;
}
void close_input() {
    binding_capture=-1;
    input_active=false;
    if(gui) {ImGui::GetIO().ClearInputMouse();ImGui::GetIO().ClearInputKeys();ImGui::GetIO().MouseDrawCursor=false;}
    if(clip_saved&&GetForegroundWindow()==window)ClipCursor(&previous_clip);
    clip_saved=false;wheel=0;
}
LRESULT CALLBACK window_proc(HWND hwnd,UINT message,WPARAM wparam,LPARAM lparam) {
    if(message==WM_ENTERSIZEMOVE)moving_window=true;
    if(message==WM_EXITSIZEMOVE)moving_window=false;
    const bool client_input=message==WM_INPUT||message==WM_CHAR||(message>=WM_MOUSEFIRST&&message<=WM_MOUSELAST)||
        (message>=WM_KEYFIRST&&message<=WM_KEYLAST)||(message==WM_SETCURSOR&&LOWORD(lparam)==HTCLIENT);
    if(client_input&&!stopping&&!moving_window&&input_active) {
        std::unique_lock lock(guard,std::try_to_lock);
        if(lock.owns_lock()&&gui&&menu_open) {
            // Hand F9 to the existing timer/menu hook and release our capture.
            if((message==WM_KEYDOWN||message==WM_SYSKEYDOWN)&&wparam==VK_F9) {
                auto saved=ImGui::GetCurrentContext();ImGui::SetCurrentContext(gui);
                menu_open=false;close_input();ImGui::SetCurrentContext(saved);
            } else {
                auto saved=ImGui::GetCurrentContext();ImGui::SetCurrentContext(gui);
                if(message==WM_INPUT) {
                    RAWINPUT raw{};UINT size=sizeof(raw);
                    if(GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam),RID_INPUT,&raw,&size,sizeof(RAWINPUTHEADER))!=static_cast<UINT>(-1)&&
                        raw.header.dwType==RIM_TYPEMOUSE&&(raw.data.mouse.usButtonFlags&RI_MOUSE_WHEEL))
                        wheel+=static_cast<SHORT>(raw.data.mouse.usButtonData)/static_cast<float>(WHEEL_DELTA);
                } else if(message==WM_MOUSEWHEEL)wheel+=static_cast<SHORT>(HIWORD(wparam))/static_cast<float>(WHEEL_DELTA);
                else if(!(message>=WM_MOUSEFIRST&&message<=WM_MOUSELAST)&&message!=WM_MOUSELEAVE)
                    ImGui_ImplWin32_WndProcHandler(hwnd,message,wparam,lparam);
                ImGui::SetCurrentContext(saved);
                if(message==WM_INPUT)return DefWindowProcW(hwnd,message,wparam,lparam);
                return message==WM_SETCURSOR?TRUE:0;
            }
        }
    }
    return previous_proc?CallWindowProcW(previous_proc,hwnd,message,wparam,lparam):DefWindowProcW(hwnd,message,wparam,lparam);
}
void cleanup() {
    if(gui) {ImGui::SetCurrentContext(gui);close_input();ImGui_ImplDX11_Shutdown();ImGui_ImplWin32_Shutdown();ImGui::DestroyContext(gui);gui=nullptr;fonts::forget();}
    if(window&&previous_proc&&reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window,GWLP_WNDPROC))==window_proc)
        SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(previous_proc));
    frame_trace::uninstall();collision_gpu::release();
    release(target);release(context);release(device);release(chain);window=nullptr;
}
bool attach(IDXGISwapChain* candidate) {
    DXGI_SWAP_CHAIN_DESC desc{};DWORD owner=0;
    if(FAILED(candidate->GetDesc(&desc))||!IsWindowVisible(desc.OutputWindow))return false;
    GetWindowThreadProcessId(desc.OutputWindow,&owner);if(owner!=GetCurrentProcessId())return false;
    if(FAILED(candidate->GetDevice(__uuidof(ID3D11Device),reinterpret_cast<void**>(&device))))return false;
    device->GetImmediateContext(&context);
    // No ImGui ini or log files: the menu's position and size are saved in T6Gnosis.cfg.
    gui=ImGui::CreateContext();ImGui::GetIO().IniFilename=nullptr;ImGui::GetIO().LogFilename=nullptr;
    ImGui::GetIO().ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    auto& colors=ImGui::GetStyle().Colors;
    colors[ImGuiCol_WindowBg]=ImVec4(0.075f,0.045f,0.115f,0.98f);
    colors[ImGuiCol_PopupBg]=ImVec4(0.10f,0.065f,0.15f,0.98f);
    colors[ImGuiCol_Border]=ImVec4(0.40f,0.25f,0.57f,0.65f);
    colors[ImGuiCol_Text]=ImVec4(0.95f,0.92f,0.99f,1.0f);
    colors[ImGuiCol_TextDisabled]=ImVec4(0.62f,0.55f,0.72f,1.0f);
    colors[ImGuiCol_TitleBg]=ImVec4(0.12f,0.065f,0.19f,1.0f);
    colors[ImGuiCol_TitleBgActive]=ImVec4(0.25f,0.12f,0.38f,1.0f);
    colors[ImGuiCol_FrameBg]=ImVec4(0.20f,0.12f,0.29f,1.0f);
    colors[ImGuiCol_FrameBgHovered]=ImVec4(0.34f,0.20f,0.47f,1.0f);
    colors[ImGuiCol_FrameBgActive]=ImVec4(0.43f,0.25f,0.60f,1.0f);
    colors[ImGuiCol_Button]=ImVec4(0.29f,0.15f,0.43f,1.0f);
    colors[ImGuiCol_ButtonHovered]=ImVec4(0.44f,0.25f,0.62f,1.0f);
    colors[ImGuiCol_ButtonActive]=ImVec4(0.55f,0.32f,0.74f,1.0f);
    colors[ImGuiCol_Header]=colors[ImGuiCol_Button];
    colors[ImGuiCol_HeaderHovered]=colors[ImGuiCol_ButtonHovered];
    colors[ImGuiCol_HeaderActive]=colors[ImGuiCol_ButtonActive];
    colors[ImGuiCol_CheckMark]=ImVec4(0.80f,0.58f,1.0f,1.0f);
    colors[ImGuiCol_SliderGrab]=ImVec4(0.66f,0.41f,0.88f,1.0f);
    colors[ImGuiCol_SliderGrabActive]=colors[ImGuiCol_CheckMark];
    colors[ImGuiCol_Separator]=colors[ImGuiCol_Border];
    colors[ImGuiCol_SeparatorHovered]=colors[ImGuiCol_ButtonHovered];
    colors[ImGuiCol_SeparatorActive]=colors[ImGuiCol_ButtonActive];
    colors[ImGuiCol_ScrollbarBg]=ImVec4(0.07f,0.04f,0.10f,1.0f);
    colors[ImGuiCol_ScrollbarGrab]=colors[ImGuiCol_Button];
    colors[ImGuiCol_ScrollbarGrabHovered]=colors[ImGuiCol_ButtonHovered];
    colors[ImGuiCol_ScrollbarGrabActive]=colors[ImGuiCol_ButtonActive];
    colors[ImGuiCol_ResizeGrip]=ImVec4(0.66f,0.41f,0.88f,0.35f);
    colors[ImGuiCol_ResizeGripHovered]=colors[ImGuiCol_ButtonHovered];
    colors[ImGuiCol_ResizeGripActive]=colors[ImGuiCol_ButtonActive];
    colors[ImGuiCol_NavHighlight]=colors[ImGuiCol_CheckMark];
    colors[ImGuiCol_TextSelectedBg]=ImVec4(0.55f,0.32f,0.74f,0.50f);
    fonts::load();
    if(!ImGui_ImplWin32_Init(desc.OutputWindow)) {ImGui::DestroyContext(gui);gui=nullptr;release(context);release(device);return false;}
    if(!ImGui_ImplDX11_Init(device,context)) {ImGui_ImplWin32_Shutdown();ImGui::DestroyContext(gui);gui=nullptr;release(context);release(device);return false;}
    chain=candidate;chain->AddRef();window=desc.OutputWindow;
    SetLastError(0);previous_proc=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(window_proc)));
    if(!previous_proc) {cleanup();return false;}
    // Pass-through observation of the game's render-target/depth sequence. Optional:
    // the overlay works without it (the depth probe then reads the view bound at Present).
    frame_trace::install(context);
    return true;
}
// Hover help for the item just drawn.
void tip(const char* text) {
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::BeginTooltip();ImGui::PushTextWrapPos(ImGui::GetFontSize()*30);ImGui::TextUnformatted(text);ImGui::PopTextWrapPos();ImGui::EndTooltip();
    }
}
// Full-width radius slider; the label lives in the format, with the derived height.
void radius_slider(const char* id,float* value,float lo,float hi) {
    char format[64]{};std::snprintf(format,sizeof(format),"Radius %%.0f  (+/- %.0f height)",vertical(*value));
    ImGui::SetNextItemWidth(-1);ImGui::SliderFloat(id,value,lo,hi,format,ImGuiSliderFlags_AlwaysClamp);
}
// Section header. status, when given, is drawn right-aligned on the bar in the
// disabled-text colour.
bool section(const char* name,bool open=true,const char* status=nullptr) {
    const bool shown=ImGui::CollapsingHeader(name,open?ImGuiTreeNodeFlags_DefaultOpen:0);
    if(status&&*status) {
        const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();const auto size=ImGui::CalcTextSize(status);
        ImGui::GetWindowDrawList()->AddText(ImVec2(hi.x-size.x-ImGui::GetStyle().FramePadding.x*2,lo.y+(hi.y-lo.y-size.y)*0.5f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled),status);
    }
    return shown;
}
// Menu layout (user request 2026-10-13): master switch, map-specific aids, Movement
// HUD, Zombies (wallhack + despawn), Spawns, Collision, Triggers, then NAV and quick
// controls. Long explanations are tooltips; finer options in the sections themselves.
void menu(const FrameInfo& frame) {
    const auto display=ImGui::GetIO().DisplaySize;
    const ImVec2 size(std::max(300.0f,std::min(560.0f,display.x-40)),std::max(280.0f,std::min(900.0f,display.y-80)));
    // Position and size persist in T6Gnosis.cfg (menu_x/y/w/h); Reset restores the defaults.
    const ImGuiCond placement=reset_window?ImGuiCond_Always:ImGuiCond_FirstUseEver;
    const bool saved_place=!reset_window&&menu_w>0&&menu_h>0;reset_window=false;
    ImGui::SetNextWindowSize(saved_place?ImVec2(menu_w,menu_h):size,placement);
    ImGui::SetNextWindowPos(saved_place?ImVec2(menu_x,menu_y):ImVec2(50,std::max(25.0f,std::min(120.0f,display.y-size.y-25))),placement);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,10);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(18,16));
    if(ImGui::Begin("T6 Gnosis " GNOSIS_VERSION "###T6 Gnosis",&menu_open,ImGuiWindowFlags_NoCollapse)) {
        const auto place=ImGui::GetWindowPos(),extent=ImGui::GetWindowSize();
        menu_x=place.x;menu_y=place.y;menu_w=extent.x;menu_h=extent.y;
        // Master switch: everything below draws only while it is on.
        ImGui::PushStyleColor(ImGuiCol_Button,visible?ImVec4(0.16f,0.55f,0.30f,1):ImVec4(0.35f,0.30f,0.40f,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,visible?ImVec4(0.22f,0.66f,0.38f,1):ImVec4(0.45f,0.38f,0.52f,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,visible?ImVec4(0.27f,0.75f,0.44f,1):ImVec4(0.52f,0.44f,0.60f,1));
        const float reset_width=ImGui::CalcTextSize("Reset").x+ImGui::GetStyle().FramePadding.x*4;
        if(ImGui::Button(visible?"OVERLAY ON":"OVERLAY OFF",ImVec2(-(reset_width+ImGui::GetStyle().ItemSpacing.x),34)))visible=!visible;
        ImGui::PopStyleColor(3);
        tip("Master switch for everything T6 Gnosis draws. The settings below are kept while it is off.");
        ImGui::SameLine();
        if(ImGui::Button("Reset",ImVec2(reset_width,34)))ImGui::OpenPopup("Reset settings");
        tip("Restore every setting, quick-control key and the menu's size and position to the defaults.");
        if(ImGui::BeginPopup("Reset settings")) {
            ImGui::TextUnformatted("Reset all settings to defaults?");
            if(ImGui::Button("Reset",ImVec2(90,0))) {reset_settings();ImGui::CloseCurrentPopup();}
            ImGui::SameLine();if(ImGui::Button("Cancel",ImVec2(90,0)))ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::Spacing();
        ImGui::BeginDisabled(!visible);

        const bool origins=frame.graph&&frame.graph->map=="maps/mp/zm_tomb.d3dbsp";
        if(origins&&section("Origins puzzle aids")) {
            ImGui::Checkbox("Wind puzzle aid",&wind_aid);
            tip("Wind puzzle 2 only. Boxes: green solved, red unsolved, yellow ignoring hits; arrow toward the destination. Ring: where the held wind staff's shot would land; green solves, red fails, yellow uncertain, gray miss.");
            if(wind_aid) {ImGui::SameLine();ImGui::Checkbox("Details##wind",&wind_details);tip("Verdict and live numbers.");}
            ImGui::Checkbox("Fire puzzle aid",&fire_aid);
            tip("Fire puzzle 2 only. Spheres: the 64-unit torch hitboxes; green correct lit, orange correct unlit, red wrong lit, gray wrong unlit. Rings: the main and two side shots; green lights a correct torch, red wrong torch, yellow ignored, gray nothing.");
            if(fire_aid) {ImGui::SameLine();ImGui::Checkbox("Details##fire",&fire_details);tip("Summary line and live numbers.");}
        }
        const bool mob_map=frame.graph&&frame.graph->map=="maps/mp/zm_prison.d3dbsp";
        if(mob_map&&section("Mob of the Dead")) {
            ImGui::Checkbox("Blue skull aid",&mob_skulls);
            tip("Blundergat quest skulls (after the Hell's Retriever): each skull's 100-unit catch sphere, green when the predicted throw catches it and cyan while a lightning-hands hit reveals it. The predicted tomahawk arc ends at the game's single check point: the first impact, or 0.55 s after the throw. The last real throw stays drawn for a few seconds. Prediction fitted from standing throws; running throws are approximate.");
            ImGui::SameLine();ImGui::Checkbox("Shock boxes (Afterlife)",&mob_boxes);
            tip("In Afterlife: each shock box's bounds and a ring at your height showing where you are within its 256-unit activation range (green when in range).");
        }

        if(section("Movement HUD")) {
            ImGui::Checkbox("Stamina bar",&show_stamina);ImGui::SameLine();
            ImGui::Checkbox("Speedometer",&show_speedometer);ImGui::SameLine();
            ImGui::Checkbox("Jump stats",&show_jump_stats);
        }

        if(section("Zombies")) {
            ImGui::Checkbox("Wallhack",&zombie_markers);
            tip("Arrows above living zombies, through walls; diamonds for special enemies. With the despawn tracker on, arrows are coloured by cleanup category.");
            despawn::ui::menu(frame.despawn_view,zombie_markers);
        }

        // Site counts ride on the Spawns bar while spawn points are drawn.
        char sites[96]{};
        if(spawn_sites&&frame.spawn_valid) {
            const auto& s=*frame.spawn_snapshot;
            std::snprintf(sites,sizeof(sites),"Sites: %u total / %u active / %u drawn",static_cast<unsigned>(s.sites.size()),s.active_count,frame.spawn_count);
        }
        if(section("Spawns",true,sites)) {
            ImGui::Checkbox("Spawn points",&spawn_sites);ImGui::SameLine();ImGui::Checkbox("Labels##spawns",&spawn_labels);
            ImGui::Checkbox("Special spawn locations",&special_spawn_sites);ImGui::SameLine();ImGui::Checkbox("Only active",&active_spawns_only);
            radius_slider("##spawn_radius",&spawn_range,400,8000);
        }

        if(section("Player collision")) {
            ImGui::Checkbox("World collision",&world_collision);
            tip("Solid faces, darker edges; only the nearest collision surface shows. World brushes with solid or player-clip contents, plus collision triangles (two-sided, coloured by their stored facing). Entity collision (doors, debris, moving parts) and model/prop collision are not drawn. The player collides as a rounded body about 15 units in radius, so it stops short of brush corners. Drawn under the game HUD; only zombies and your weapon cover it.");
            ImGui::SameLine();ImGui::Checkbox("Zombie hitboxes",&zombie_boxes);
            tip("Each living zombie's entity bounds, the box the game uses for its collision. Works with or without world collision.");
            ImGui::SameLine();ImGui::Checkbox("bbox",&player_bounds);
            tip("The player entity's axis-aligned bounds (30 x 30 x 70). Movement collides as a round body of radius 15, so the corners reach further than you do.");
            // Colour key for the collision surfaces, all always drawn.
            if(world_collision) {
                ImGui::TextColored(ImVec4(0.3f,0.9f,0.47f,1),"Floors");
                ImGui::SameLine();ImGui::TextColored(ImVec4(0.3f,0.78f,1,1),"Walls");
                ImGui::SameLine();ImGui::TextColored(ImVec4(1,0.63f,0.24f,1),"Roofs");
                ImGui::SameLine();ImGui::TextColored(ImVec4(1,0.31f,1,1),"Invisible");
                tip("Clip-only brushes: they block the player but have no visible surface.");
            }
            if(world_collision) {
                radius_slider("##collision_radius",&collision_range,100,3000);
                ImGui::SetNextItemWidth(-1);ImGui::SliderFloat("##collision_opacity",&collision_opacity,0.2f,1.0f,"Opacity %.2f",ImGuiSliderFlags_AlwaysClamp);
            }
        }

        if(section("Triggers")) {
            ImGui::Checkbox("Trigger geometry",&trigger_positions);
            tip("Native boxes, vertical cylinders and decoded brush hulls; red shapes draw through walls. A shape is drawn whole when any part overlaps the radius. Unsupported geometry is skipped.");
            ImGui::SameLine();ImGui::Checkbox("Labels##triggers",&trigger_labels);ImGui::SameLine();ImGui::Checkbox("Playable area",&playable_areas);
            tip("Green: enabled player volumes; bright green: life brushes; red: kill brushes; gray: other enabled overlays. Life brushes override kill regions. Decoded base monitor volumes only; zone state updates once per second.");
            radius_slider("##trigger_radius",&trigger_range,400,8000);
        }

        if(section("NAV",false)) {
            ImGui::Checkbox("Nodes",&nodes);ImGui::SameLine();ImGui::Checkbox("Links",&links);ImGui::SameLine();
            ImGui::Checkbox("IDs",&labels);ImGui::SameLine();ImGui::Checkbox("Directions",&arrows);
            radius_slider("##nav_radius",&range,400,8000);
            ImGui::TextDisabled("Graph: %u nodes | %u links",frame.graph?frame.graph->count:0,frame.graph?frame.graph->directed_links:0);
        }
        ImGui::EndDisabled();

        if(ImGui::CollapsingHeader("Quick controls")) {
            ImGui::TextWrapped("One key per top-level option: click a button, then press a key. Escape cancels, Backspace clears. Keys are saved with the settings.");
            for(std::size_t i=0;i<quick.size();++i) {
                ImGui::PushID(static_cast<int>(i));char name[64]{};key_label(quick[i].key,name,sizeof(name));
                if(ImGui::Button(binding_capture==static_cast<int>(i)?"Press key...":name,ImVec2(110,0))) {
                    binding_capture=static_cast<int>(i);
                    for(int key=0;key<256;++key)down[static_cast<std::size_t>(key)]=(GetAsyncKeyState(key)&0x8000)!=0;
                }
                ImGui::SameLine();ImGui::TextUnformatted(quick[i].label);ImGui::PopID();
            }
        }
        ImGui::Spacing();
        if(ImGui::Button("Close menu",ImVec2(-1,30)))menu_open=false;
    }
    ImGui::End();ImGui::PopStyleVar(2);
}
ImVec2 screen(nav::Point p,float sx,float sy) {return {p.x*sx,p.y*sy};}
// Start-up hint (user requests 2026-10-20): a few seconds after the overlay first draws,
// a small top-left banner names the menu key for 10 s, its key cap brightening slowly
// (4 s cycle), then fades out. Once per launch; opening the menu ends it.
constexpr std::uint64_t hint_delay_ms=3000,hint_show_ms=10000,hint_fade_ms=1000;
std::uint64_t first_frame_at=0;bool hint_done=false;
void draw_hint(std::uint64_t now,float height) {
    if(!first_frame_at)first_frame_at=now;
    if(menu_open)hint_done=true;
    const auto age=now-first_frame_at;
    if(hint_done||age<hint_delay_ms)return;
    const auto shown=age-hint_delay_ms;
    if(shown>hint_show_ms) {hint_done=true;return;}
    const float fade_in=std::min(1.0f,static_cast<float>(shown)/250.0f);
    const float fade_out=std::min(1.0f,static_cast<float>(hint_show_ms-shown)/static_cast<float>(hint_fade_ms));
    const float alpha=std::clamp(std::min(fade_in,fade_out),0.0f,1.0f);
    const float pulse=0.5f+0.5f*std::sin(static_cast<float>(shown)*6.2831853f/4000.0f);  // one slow cycle per 4 s
    const float scale=height/1080.0f,size=22.0f*scale,pad=8*scale;
    auto* font=fonts::get(size);auto* list=ImGui::GetForegroundDrawList();
    const char* key="F8";const char* message="opens the T6 Gnosis menu";
    const auto key_size=font->CalcTextSizeA(size,10000,0,key),text_size=font->CalcTextSizeA(size,10000,0,message);
    const float cap_width=key_size.x+pad*2,total=cap_width+pad+text_size.x;
    const ImVec2 top(24*scale,24*scale);
    auto a=[&](float value) {return static_cast<int>(value*alpha);};
    // Banner plate, then the key cap (pulsing purple to light purple) and the message.
    list->AddRectFilled(ImVec2(top.x-pad*1.5f,top.y-pad),ImVec2(top.x+total+pad*1.5f,top.y+key_size.y+pad),IM_COL32(19,11,29,a(230)),6*scale);
    list->AddRect(ImVec2(top.x-pad*1.5f,top.y-pad),ImVec2(top.x+total+pad*1.5f,top.y+key_size.y+pad),IM_COL32(102,64,145,a(200)),6*scale,0,1.5f*scale);
    const int r=static_cast<int>(110+90*pulse),g=static_cast<int>(60+80*pulse),b=static_cast<int>(170+70*pulse);
    list->AddRectFilled(ImVec2(top.x,top.y-pad*0.5f),ImVec2(top.x+cap_width,top.y+key_size.y+pad*0.5f),IM_COL32(r,g,b,a(255)),4*scale);
    list->AddText(font,size,ImVec2(top.x+pad,top.y),IM_COL32(255,255,255,a(255)),key);
    list->AddText(font,size,ImVec2(top.x+cap_width+pad+scale,top.y+scale),IM_COL32(0,0,0,a(200)),message);
    list->AddText(font,size,ImVec2(top.x+cap_width+pad,top.y),IM_COL32(240,236,250,a(255)),message);
}
// Solo pause freezes the script VM clock (0x2DEA810, 50 ms ticks, the clock the
// despawn countdown reads). No change for 200 ms means the pause menu is open.
bool game_paused(std::uint64_t now) {
    static const bool vm=gsc_args::bindings_match();
    static std::uint32_t last_clock=0;static std::uint64_t changed=0;
    std::uint32_t clock=0;
    if(!vm||!gsc_args::read(0x2DEA810,clock))return false;
    if(clock!=last_clock) {last_clock=clock;changed=now;return false;}
    return now-changed>200;
}
// Scales the alpha of every vertex added to the list since index first.
void fade(ImDrawList& list,int first,float strength) {
    for(int i=first;i<list.VtxBuffer.Size;++i) {
        auto& color=list.VtxBuffer[i].col;
        const auto alpha=static_cast<ImU32>(static_cast<float>((color>>IM_COL32_A_SHIFT)&0xFF)*std::clamp(strength,0.0f,1.0f));
        color=(color&~IM_COL32_A_MASK)|(alpha<<IM_COL32_A_SHIFT);
    }
}
        void draw_readouts(float width,float height)
        {
            const auto s=practice::hud();
            if (!s.visible || GetTickCount64()-s.updated>1000 || !std::isfinite(s.speed) || s.capacity<=0) return;
            const float scale=height/1080.0f;
            // Bar 206 units above the bottom (was 146): the bottom row, jump speed, now ends
            // near y 960 of 1080, clear of the power-up icons from about 1030 (user
            // screenshot 2026-10-13: the jump-speed row covered the Insta-Kill skull).
            const float cx=width*0.5f, y=height-206.0f*scale;
            const float half=67.5f*scale, thick=4.5f*scale, font_size=29.25f*scale;  // 39 until 2026-10-13 (user: 25% smaller)
            auto* list=ImGui::GetBackgroundDrawList();
            if (show_stamina)
            {
            list->AddRectFilled(ImVec2(cx-half,y-thick*0.5f),ImVec2(cx+half,y+thick*0.5f),IM_COL32(25,25,25,210));
            const float fraction=std::clamp(s.remaining/s.capacity,0.0f,1.0f);
            const auto bar_color=s.remaining>s.threshold-100 ? IM_COL32(64,217,115,255) : IM_COL32(255,140,51,255);
            if (fraction>0)
                list->AddRectFilled(ImVec2(cx-half,y-thick*0.5f),ImVec2(cx-half+2*half*fraction,y+thick*0.5f),bar_color);
            }
            auto text=[&](const char* value,float top,ImU32 color,float font_scale=1.0f)
            {
                const float actual_font_size=font_size*font_scale;
                const auto size=fonts::get(actual_font_size)->CalcTextSizeA(actual_font_size,10000,0,value);
                const ImVec2 at(cx-size.x*0.5f,top);
                list->AddText(fonts::get(actual_font_size),actual_font_size,ImVec2(at.x+scale,at.y+scale),IM_COL32(0,0,0,200),value);
                list->AddText(fonts::get(actual_font_size),actual_font_size,at,color,value);
            };
            char label[96]{};
            if (show_jump_stats && s.steps>=0)
            {
                std::snprintf(label,sizeof(label),"%d frame %dms",s.steps,s.milliseconds);
                text(label,y-thick*0.5f-4*scale-font_size,IM_COL32(240,240,240,255));
            }
            if (show_speedometer)
            {
            const float min_speed=s.longer_sprint ? 310.0f : 260.0f;
            const float max_speed=s.longer_sprint ? 390.0f : 350.0f;
            const float ratio=std::clamp((std::floor(s.speed)-min_speed)/(max_speed-min_speed),0.0f,1.0f);
            const float t=ratio<0.5f ? ratio*2 : (ratio-0.5f)*2;
            const ImVec4 color=ratio<0.5f ? ImVec4(0.6f+0.4f*t,1,0.6f,1) : ImVec4(1,1-0.8f*t,0.6f-0.6f*t,1);
            std::snprintf(label,sizeof(label),"%d",static_cast<int>(s.speed));
            text(label,y+thick*0.5f+4*scale,ImGui::ColorConvertFloat4ToU32(color));
            }
            if (show_jump_stats && s.jump_speed>=0)
            {
                if (s.has_cancel_delta)
                    std::snprintf(label,sizeof(label),"%.0f (%+.0f)",std::floor(s.jump_speed),std::round(s.cancel_delta));
                else
                    std::snprintf(label,sizeof(label),"%.0f",std::floor(s.jump_speed));
                text(label,y+thick*0.5f+8*scale+font_size,IM_COL32(220,225,230,255));
            }
        }

void draw_player_box(ImDrawList* list,const nav::Camera& cam,const triggers::PlayerBox& player,float sx,float sy) {
 const auto lo=player.origin+player.mins,hi=player.origin+player.maxs;
 const nav::Vec3 corners[]={{lo.x,lo.y,lo.z},{hi.x,lo.y,lo.z},{hi.x,hi.y,lo.z},{lo.x,hi.y,lo.z},{lo.x,lo.y,hi.z},{hi.x,lo.y,hi.z},{hi.x,hi.y,hi.z},{lo.x,hi.y,hi.z}};
 const unsigned edges[][2]={{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
 for(const auto& edge:edges){auto a=nav::camera_space(cam,corners[edge[0]]),b=nav::camera_space(cam,corners[edge[1]]);if(nav::clip_segment(cam,a,b))list->AddLine(screen(nav::project(cam,a),sx,sy),screen(nav::project(cam,b),sx,sy),IM_COL32(70,220,255,255),2.0f);}
}
// World collision as solid faces inside a box around the eye. Painter's algorithm:
// back faces are culled and the rest drawn far to near, so only the nearest collision
// surface shows (overlaps are hidden fully at opacity 1). Fill is shaded by how
// directly a face looks at the eye; its edges are a darker shade of the same colour.
unsigned draw_collision(ImDrawList* list,const nav::Camera& cam,const collision::Snapshot& s,float sx,float sy) {
 constexpr unsigned budget=15000;
 const auto c=cam.origin;const float h=collision_range,v=vertical(collision_range);
 const int x0=static_cast<int>(std::floor((c.x-h)/collision::cell_size)),x1=static_cast<int>(std::floor((c.x+h)/collision::cell_size));
 const int y0=static_cast<int>(std::floor((c.y-h)/collision::cell_size)),y1=static_cast<int>(std::floor((c.y+h)/collision::cell_size));
 std::vector<std::pair<float,std::uint32_t>> order;
 for(int ix=x0;ix<=x1;++ix)for(int iy=y0;iy<=y1;++iy) {
  const auto cell=s.cells.find(collision::cell_key((ix+0.5f)*collision::cell_size,(iy+0.5f)*collision::cell_size));
  if(cell==s.cells.end())continue;
  for(const auto index:cell->second) {
   const auto& f=s.faces[index];
   if(std::abs(f.centre.x-c.x)>h||std::abs(f.centre.y-c.y)>h||std::abs(f.centre.z-c.z)>v)continue;
   const auto to=f.centre-c;
   if(nav::dot(f.normal,to)>=0)continue;  // back face: the far side of a brush or a downward-facing triangle
   order.push_back({nav::dot(to,cam.forward),index});
  }
 }
 std::sort(order.begin(),order.end(),[](const auto& a,const auto& b){return a.first>b.first;});
 if(order.size()>budget)order.erase(order.begin(),order.end()-budget);  // keep the nearest
 const auto alpha=static_cast<unsigned>(std::clamp(collision_opacity,0.0f,1.0f)*255.0f+0.5f);
 std::vector<ImVec2> pixels;unsigned drawn=0;
 for(const auto& [depth,index]:order) {
  const auto& f=s.faces[index];
  std::vector<nav::Vec3> points;points.reserve(f.points.size());
  for(const auto p:f.points)points.push_back(nav::camera_space(cam,p));
  pixels.clear();
  for(const auto p:nav::clip_polygon(cam,points))pixels.push_back(screen(nav::project(cam,p),sx,sy));
  if(pixels.size()<3)continue;
  float r=80,g=200,b=255;
  switch(f.kind) {
  case collision::Kind::floor:r=80;g=230;b=120;break;
  case collision::Kind::roof:r=255;g=160;b=60;break;
  case collision::Kind::clip:r=255;g=80;b=255;break;
  default:break;
  }
  const auto to=f.centre-c;const float distance=std::sqrt(nav::dot(to,to));
  const float facing=distance>1?-nav::dot(f.normal,to)/distance:1;
  const float shade=0.45f+0.55f*std::clamp(facing,0.0f,1.0f);
  const auto fill=IM_COL32(static_cast<int>(r*shade),static_cast<int>(g*shade),static_cast<int>(b*shade),alpha);
  const auto edge=IM_COL32(static_cast<int>(r*shade*0.45f),static_cast<int>(g*shade*0.45f),static_cast<int>(b*shade*0.45f),std::min(255u,alpha+40u));
  const int count=static_cast<int>(pixels.size());
  list->AddConvexPolyFilled(pixels.data(),count,fill);
  list->AddPolyline(pixels.data(),count,edge,ImDrawFlags_Closed,1.0f);
  ++drawn;
 }
 return drawn;
}
// GPU path for the collision view: the same faces as draw_collision, turned into
// clip-space vertices for collision_gpu (exact per-pixel occlusion, no stacking).
// Triangles are two-sided: their winding is not reliable, so each is oriented toward
// the eye and classified from there (horizontal below you = floor, above = roof).
// Brush faces keep their outward normals and are culled when facing away.
unsigned build_collision(const nav::Camera& cam,const collision::Snapshot& s,float sx,float sy,collision_gpu::Batch& batch) {
 constexpr unsigned budget=40000;
 batch.viewport_x=cam.x*sx;batch.viewport_y=cam.y*sy;batch.viewport_width=cam.width*sx;batch.viewport_height=cam.height*sy;
 const auto c=cam.origin;const float h=collision_range,v=vertical(collision_range);
 const int x0=static_cast<int>(std::floor((c.x-h)/collision::cell_size)),x1=static_cast<int>(std::floor((c.x+h)/collision::cell_size));
 const int y0=static_cast<int>(std::floor((c.y-h)/collision::cell_size)),y1=static_cast<int>(std::floor((c.y+h)/collision::cell_size));
 // Candidates: faces listed in the cells under the range box, plus the very large ones.
 std::vector<std::uint32_t> candidates(s.large);
 for(int ix=x0;ix<=x1;++ix)for(int iy=y0;iy<=y1;++iy) {
  const auto cell=s.cells.find(collision::cell_key((ix+0.5f)*collision::cell_size,(iy+0.5f)*collision::cell_size));
  if(cell!=s.cells.end())candidates.insert(candidates.end(),cell->second.begin(),cell->second.end());
 }
 std::sort(candidates.begin(),candidates.end());candidates.erase(std::unique(candidates.begin(),candidates.end()),candidates.end());
 // Keep a face if any part of its bounds is in the range box; rank by the distance to
 // its nearest point, so tall or wide faces are not dropped for a far-away centre.
 std::vector<std::pair<float,std::uint32_t>> chosen;
 for(const auto index:candidates) {
  const auto& f=s.faces[index];
  if(f.hi.x<c.x-h||f.lo.x>c.x+h||f.hi.y<c.y-h||f.lo.y>c.y+h||f.hi.z<c.z-v||f.lo.z>c.z+v)continue;
  if(!f.triangle&&nav::dot(f.normal,f.centre-c)>=0)continue;
  const float dx=std::max({f.lo.x-c.x,0.0f,c.x-f.hi.x}),dy=std::max({f.lo.y-c.y,0.0f,c.y-f.hi.y}),dz=std::max({f.lo.z-c.z,0.0f,c.z-f.hi.z});
  chosen.push_back({dx*dx+dy*dy+dz*dz,index});
 }
 if(chosen.size()>budget) {
  std::nth_element(chosen.begin(),chosen.begin()+budget,chosen.end(),[](const auto& a,const auto& b){return a.first<b.first;});
  chosen.resize(budget);
 }
 const float strength=std::clamp(collision_opacity,0.0f,1.0f)*(frame_paused?std::clamp(paused_strength,0.0f,1.0f):1.0f);
 const auto alpha=static_cast<unsigned>(strength*255.0f+0.5f);
 constexpr float depth_scale=collision_gpu::far_plane/(collision_gpu::far_plane-collision_gpu::near_plane);
 // offset moves the point along its eye ray by that many world units (positive is
 // away from the eye) without changing its screen position.
 auto vertex=[&](nav::Vec3 world,float offset,ImU32 color) {
  auto p=nav::camera_space(cam,world);
  if(const float length=std::sqrt(nav::dot(p,p));length>1)p=p*((length+offset)/length);
  return collision_gpu::Vertex{-p.x/cam.tan_x,p.y/cam.tan_y,depth_scale*(p.z-collision_gpu::near_plane),p.z,color};
 };
 unsigned drawn=0;
 for(const auto& item:chosen) {const auto index=item.second;
  const auto& f=s.faces[index];
  if(!f.blocking)continue;  // non-blocking faces are not collision
  auto n=f.normal;const auto kind=f.kind;const auto to=f.centre-c;
  // Triangles are drawn two-sided and shaded by the side facing the eye. Their colour
  // follows the stored winding (floor stays floor when you are below it, e.g. prone;
  // 453 of 508 near-horizontal triangles near one test spot agree). Both sides draw
  // solid: the one pass-through observation (triangles 66884/66885) turned out to be
  // entity-owned, model-local data, so nothing shows world triangles are one-sided.
  // The back side only loses ties.
  bool back=false;
  if(f.triangle) {back=nav::dot(n,to)>0;n=back?n*-1.0f:n;}
  float r=80,g=200,b=255;
  switch(kind) {
  case collision::Kind::floor:r=80;g=230;b=120;break;
  case collision::Kind::roof:r=255;g=160;b=60;break;
  case collision::Kind::clip:r=255;g=80;b=255;break;
  default:break;
  }
  if(!f.blocking) {r=150;g=150;b=150;}
  const float distance=std::sqrt(nav::dot(to,to));
  // Shade from the normal against the view direction, so coplanar faces (several
  // brushes often share one wall plane) get the same colour and their depth ties
  // cannot show as shimmering stripes; it used the direction to each face's centre,
  // which differs per face (user report 2026-10-13, blue wall).
  const float shade=0.6f+0.4f*std::clamp(-nav::dot(n,cam.forward),0.0f,1.0f);
  const auto fill=IM_COL32(static_cast<int>(r*shade),static_cast<int>(g*shade),static_cast<int>(b*shade),alpha);
  const auto edge=IM_COL32(static_cast<int>(r*shade*0.45f),static_cast<int>(g*shade*0.45f),static_cast<int>(b*shade*0.45f),std::min(255u,alpha+40u));
  // Fixed draw priority for coincident faces: solid faces win over clip-only, which
  // win over back sides and non-blocking faces. The step is in world units, not a
  // share of the distance: a share let a floor 0.2 units under a clip brush win far
  // away and lose nearby, so the colours shifted as the player moved (user report
  // 2026-10-13). Faces more than a step apart always show the truly nearer one.
  const float push=!f.blocking?3.0f:back?2.0f:kind==collision::Kind::clip?1.0f:0.0f;
  // Edges sit just in front of their own face (D24 depth resolves about 0.06 units at
  // 2000) and never reach the next priority step.
  const float edge_pull=push-std::min(0.3f+0.0002f*distance,0.8f);
  const auto& p=f.points;
  for(std::size_t i=1;i+1<p.size();++i) {
   batch.fills.push_back(vertex(p[0],push,fill));batch.fills.push_back(vertex(p[i],push,fill));batch.fills.push_back(vertex(p[i+1],push,fill));
  }
  for(std::size_t i=0;i<p.size();++i) {batch.lines.push_back(vertex(p[i],edge_pull,edge));batch.lines.push_back(vertex(p[(i+1)%p.size()],edge_pull,edge));}
  ++drawn;
 }
 return drawn;
}
// Living zombies' entity bounds as red boxes for the GPU pass (depth-tested with the
// world collision when that is drawn too). Independent of the world collision view
// (user request 2026-10-20); drawn within 3000 units, at the collision opacity.
void build_zombie_boxes(const nav::Camera& cam,collision_gpu::Batch& batch) {
 constexpr float depth_scale=collision_gpu::far_plane/(collision_gpu::far_plane-collision_gpu::near_plane);
 auto vertex=[&](nav::Vec3 world,float offset,ImU32 color) {
  auto p=nav::camera_space(cam,world);
  if(const float length=std::sqrt(nav::dot(p,p));length>1)p=p*((length+offset)/length);
  return collision_gpu::Vertex{-p.x/cam.tan_x,p.y/cam.tan_y,depth_scale*(p.z-collision_gpu::near_plane),p.z,color};
 };
 const auto c=cam.origin;constexpr float h=3000,v=3000;
 const float strength=std::clamp(collision_opacity,0.0f,1.0f)*(frame_paused?paused_strength:1.0f);
 const auto alpha=static_cast<unsigned>(strength*255.0f+0.5f);
 zombie_boxes_drawn=0;
 const auto z=zombies::snapshot();
 if(z&&GetTickCount64()-z->updated<=200) {
  const unsigned quads[6][4]={{0,3,2,1},{4,5,6,7},{0,1,5,4},{1,2,6,5},{2,3,7,6},{3,0,4,7}};
  const nav::Vec3 normals[6]={{0,0,-1},{0,0,1},{0,-1,0},{1,0,0},{0,1,0},{-1,0,0}};
  for(const auto& marker:z->markers) {
   nav::Vec3 origin{},mins{},maxs{};
   if(!zombies::living_bounds(marker,origin,mins,maxs))continue;
   if(std::abs(origin.x-c.x)>h||std::abs(origin.y-c.y)>h||std::abs(origin.z-c.z)>v)continue;
   const auto lo=origin+mins,hi=origin+maxs;
   const nav::Vec3 corners[8]={{lo.x,lo.y,lo.z},{hi.x,lo.y,lo.z},{hi.x,hi.y,lo.z},{lo.x,hi.y,lo.z},{lo.x,lo.y,hi.z},{hi.x,lo.y,hi.z},{hi.x,hi.y,hi.z},{lo.x,hi.y,hi.z}};
   const auto centre=(lo+hi)*0.5f,to=centre-c;const float distance=std::sqrt(nav::dot(to,to));
   for(unsigned q=0;q<6;++q) {
    const float shade=0.45f+0.55f*std::clamp(distance>1?-nav::dot(normals[q],to)/distance:1.0f,0.0f,1.0f);
    // Faint fill: the zombie stands inside its own box, and a solid front face hid it
    // (user report 2026-10-13). The edges carry the shape.
    const auto fill=IM_COL32(static_cast<int>(255*shade),static_cast<int>(60*shade),static_cast<int>(60*shade),alpha/5);
    const auto edge=IM_COL32(static_cast<int>(255*shade),static_cast<int>(70*shade),static_cast<int>(70*shade),std::min(255u,alpha+40u));
    const auto& f=quads[q];
    for(unsigned i=1;i<3;++i) {batch.fills.push_back(vertex(corners[f[0]],0,fill));batch.fills.push_back(vertex(corners[f[i]],0,fill));batch.fills.push_back(vertex(corners[f[i+1]],0,fill));}
    for(unsigned i=0;i<4;++i) {batch.lines.push_back(vertex(corners[f[i]],-0.3f,edge));batch.lines.push_back(vertex(corners[f[(i+1)%4]],-0.3f,edge));}
   }
   ++zombie_boxes_drawn;
  }
 }
}
// NAV graph for the GPU pass (user request 2026-10-13: draw it under zombies). Same
// selection and look as the ImGui drawing in draw(): links (reciprocal pairs once,
// traversal arrows), then node symbols sized in pixels. Returns the nodes drawn.
unsigned build_nav(const nav::Camera& cam,const nav::Graph& graph,float sx,float sy,collision_gpu::Batch& batch) {
 constexpr float depth_scale=collision_gpu::far_plane/(collision_gpu::far_plane-collision_gpu::near_plane);
 auto vertex=[&](nav::Vec3 p,ImU32 color) {  // p in camera space
  return collision_gpu::Vertex{-p.x/cam.tan_x,p.y/cam.tan_y,depth_scale*(p.z-collision_gpu::near_plane),p.z,color};
 };
 // Clip-space point moved by a pixel offset (screen y down).
 auto offset=[&](collision_gpu::Vertex v,float dx,float dy) {
  v.x+=dx*2/batch.viewport_width*v.w;v.y-=dy*2/batch.viewport_height*v.w;return v;
 };
 const auto& nodes_list=graph.nodes;
 std::vector<nav::Vec3> projected(nodes_list.size());std::vector<unsigned char> keep(nodes_list.size());
 for(std::size_t i=0;i<nodes_list.size();++i) {
  keep[i]=static_cast<unsigned char>(nodes&&nav::within_draw_range(nodes_list[i].origin,cam.origin,range,vertical(range)));  // links need nodes on, as in draw()
  if(keep[i])projected[i]=nav::camera_space(cam,nodes_list[i].origin);
 }
 // Behind the pause menu the GPU copy dims like the ImGui drawing (fade()).
 const float strength=frame_paused?paused_strength:1.0f;
 const ImU32 link_color=IM_COL32(90,205,229,static_cast<int>(150*strength));
 if(links)for(std::size_t i=0;i<nodes_list.size();++i) {
  if(!keep[i])continue;
  for(const auto dest:nodes_list[i].links) {
   if(!keep[dest])continue;
   const bool crossing=nav::traversal_crossing(nodes_list[i].type,nodes_list[dest].type);
   const bool opposite_crossing=nav::traversal_crossing(nodes_list[dest].type,nodes_list[i].type);
   if(dest<i&&!(arrows&&(crossing||opposite_crossing))&&std::find(nodes_list[dest].links.begin(),nodes_list[dest].links.end(),static_cast<std::uint16_t>(i))!=nodes_list[dest].links.end())continue;
   auto a=projected[i],b=projected[dest];
   if(!nav::clip_segment(cam,a,b))continue;
   batch.overlay_lines.push_back(vertex(a,link_color));batch.overlay_lines.push_back(vertex(b,link_color));
   if(arrows&&crossing) {
    const auto p=screen(nav::project(cam,a),sx,sy),q=screen(nav::project(cam,b),sx,sy);
    const float dx=q.x-p.x,dy=q.y-p.y,len=std::sqrt(dx*dx+dy*dy);
    if(len>0.001f) {
     const float ux=dx/len,uy=dy/len;const auto from=nodes_list[i].origin,to=nodes_list[dest].origin;
     for(const float fraction:{0.25f,0.50f,0.75f}) {
      const auto anchor=nav::camera_space(cam,from+(to-from)*fraction);
      if(!nav::point_visible(cam,anchor))continue;
      const auto tip=vertex(anchor,link_color);
      batch.overlay_lines.push_back(tip);batch.overlay_lines.push_back(offset(tip,-ux*6-uy*3,-uy*6+ux*3));
      batch.overlay_lines.push_back(tip);batch.overlay_lines.push_back(offset(tip,-ux*6+uy*3,-uy*6-ux*3));
     }
    }
   }
  }
 }
 unsigned drawn=0;
 if(nodes)for(std::size_t i=0;i<nodes_list.size();++i) {
  if(!keep[i]||!nav::point_visible(cam,projected[i]))continue;
  const auto type=nodes_list[i].type;const ImU32 color=IM_COL32(174,150,255,static_cast<int>(235*strength));
  const auto c=vertex(projected[i],color);auto& f=batch.overlay_fills;
  if(type==17) {  // filled diamond
   const auto top=offset(c,0,-6.75f),right=offset(c,6.75f,0),bottom=offset(c,0,6.75f),left=offset(c,-6.75f,0);
   f.insert(f.end(),{top,right,bottom,top,bottom,left});
  } else if(type==18) {  // square outline, 2.25 px thick
   constexpr float o=6+1.125f,in=6-1.125f;
   const float outer[4][2]={{-o,-o},{o,-o},{o,o},{-o,o}},inner[4][2]={{-in,-in},{in,-in},{in,in},{-in,in}};
   for(int k=0;k<4;++k) {
    const int n=(k+1)%4;
    const auto a=offset(c,outer[k][0],outer[k][1]),b=offset(c,outer[n][0],outer[n][1]),d=offset(c,inner[k][0],inner[k][1]),e=offset(c,inner[n][0],inner[n][1]);
    f.insert(f.end(),{a,b,e,a,e,d});
   }
  } else {  // dot, radius 2.5
   for(int k=0;k<8;++k) {
    const float a0=6.2831853f*k/8,a1=6.2831853f*(k+1)/8;
    f.insert(f.end(),{c,offset(c,2.5f*std::cos(a0),2.5f*std::sin(a0)),offset(c,2.5f*std::cos(a1),2.5f*std::sin(a1))});
   }
  }
  ++drawn;
 }
 return drawn;
}
// Runs inside the game's frame, right after its scene composite draw onto the
// backbuffer and before any HUD draw (frame_trace). Rebuilds the collision batch from
// the current camera and draws it, tested against the scene depth so zombies, the
// weapon and props cover it; the HUD is drawn afterwards by the game.
void draw(ImDrawList* list,float width,float height,FrameInfo& frame,bool world,bool collision_part);
// Playable area: the merged surface of every enabled and life volume on the map
// (playable_area.hpp). The trigger snapshot is replaced twice a second, so the volumes
// are fingerprinted and the surface is rebuilt only when they change (a zone opening,
// a new map). The build runs on a worker thread; until it finishes, the previous
// surface stays drawn (none after a map change), so a rebuild never stalls a frame
// (user report 2026-10-20: rebuilding on every snapshot made the game lag).
struct AreaSurface {std::uint64_t fingerprint=0;std::vector<area::Piece> pieces;std::vector<area::Edge> edges;};
struct AreaState {
    std::shared_ptr<const triggers::Snapshot> source;std::shared_ptr<const nav::Graph> map;
    AreaSurface surface;std::future<AreaSurface> pending;std::uint64_t pending_fingerprint=0;
} area_state;
const AreaSurface& playable_surface(const std::shared_ptr<const triggers::Snapshot>& snapshot) {
    auto& a=area_state;
    if(a.pending.valid()&&a.pending.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
        auto built=a.pending.get();
        if(built.fingerprint==a.pending_fingerprint)a.surface=std::move(built);
    }
    if(a.source==snapshot)return a.surface;
    a.source=snapshot;
    if(!a.map||!snapshot->map||!spawns::same_map(*a.map,*snapshot->map)) {a.map=snapshot->map;a.surface={};a.pending_fingerprint=0;}  // a running build for the old map is dropped
    std::vector<area::Hull> hulls;std::uint64_t fingerprint=1469598103934665603ull;  // FNV-1a
    auto mix=[&](std::int64_t value) {for(int i=0;i<8;++i){fingerprint^=static_cast<std::uint64_t>(value>>(i*8))&0xFF;fingerprint*=1099511628211ull;}};
    for(const auto& marker:snapshot->markers) {
        const auto role=triggers::region(marker.fields);
        if(role!=triggers::Region::enabled_player&&role!=triggers::Region::life)continue;
        nav::Vec3 origin{},angles{};if(!triggers::current_pose(marker,origin,angles))continue;
        for(const auto& mesh:marker.meshes) {
            std::vector<std::vector<nav::Vec3>> faces;
            for(const auto& face:mesh.faces) {
                std::vector<nav::Vec3> points;
                for(const auto v:face) {
                    const auto p=origin+(marker.shape=="Cylinder"?v:zombies::rotate(v,angles));points.push_back(p);
                    mix(std::lround(p.x*4));mix(std::lround(p.y*4));mix(std::lround(p.z*4));
                }
                faces.push_back(std::move(points));
            }
            mix(role==triggers::Region::life?2:1);
            area::Hull hull;if(area::make_hull(std::move(faces),role==triggers::Region::life,hull))hulls.push_back(std::move(hull));
        }
    }
    if(fingerprint==a.surface.fingerprint||(a.pending.valid()&&fingerprint==a.pending_fingerprint))return a.surface;
    // One build at a time: a newer change is picked up by a later snapshot once it ends.
    if(a.pending.valid())return a.surface;
    a.pending_fingerprint=fingerprint;
    a.pending=std::async(std::launch::async,[hulls=std::move(hulls),fingerprint] {
        AreaSurface built;built.fingerprint=fingerprint;area::build_surface(hulls,built.pieces,built.edges);return built;
    });
    return a.surface;
}
// FrameInfo for the injection's world drawing, from the injection camera.
FrameInfo world_frame(const std::shared_ptr<const nav::Graph>& g,const nav::Camera& cam) {
    FrameInfo frame{};frame.graph=g;frame.camera=cam;frame.camera_valid=true;
    const auto now=GetTickCount64();
    frame.player_valid=player_bounds&&triggers::player_box(frame.player);
    frame.despawn_view=last_despawn_view;
    frame.spawn_snapshot=spawns::snapshot();
    frame.trigger_snapshot=triggers::snapshot();
    if(frame.trigger_snapshot&&!triggers::fresh(*frame.trigger_snapshot,*g,now))frame.trigger_snapshot.reset();
    const auto& s=frame.spawn_snapshot;
    frame.spawn_valid=s&&now-s->updated<=2000&&s->map->map==g->map&&s->map->base==g->base&&s->map->data==g->data;
    return frame;
}
void inject_collision() {
    std::lock_guard lock(guard);
    nav_injected=false;world_injected=false;
    const bool nav_wanted=nodes||links;
    if(stopping||!visible||!gui||!device||!context||!target||!backbuffer_width||!backbuffer_height)return;
    const auto g=nav::graph();if(!g)return;
    nav::Camera cam{};if(!nav::read_camera(cam,*g))return;
    const float sx=static_cast<float>(backbuffer_width)/static_cast<float>(cam.x+cam.width);
    const float sy=static_cast<float>(backbuffer_height)/static_cast<float>(cam.y+cam.height);
    collision_gpu::Batch batch;
    batch.viewport_x=cam.x*sx;batch.viewport_y=cam.y*sy;batch.viewport_width=cam.width*sx;batch.viewport_height=cam.height*sy;
    if(world_collision)if(const auto walls=collision::snapshot())collision_drawn=build_collision(cam,*walls,sx,sy,batch);
    if(!collision_gpu::ready(device))return;
    if(zombie_boxes)build_zombie_boxes(cam,batch);
    // The NAV graph rides in the same pass so zombies and the weapon cover it; draw()
    // skips its ImGui copy when this ran.
    if(nav_wanted) {build_nav(cam,*g,sx,sy,batch);nav_injected=true;}
    // Every other world drawing (spawns, triggers, aids, zombie markers, labels) goes
    // into a private ImGui draw list with this frame's camera and is drawn here too,
    // under the weapon and HUD (user request 2026-10-14). Present then skips it.
    const float width=static_cast<float>(backbuffer_width),height=static_cast<float>(backbuffer_height);
    auto* saved_context=ImGui::GetCurrentContext();ImGui::SetCurrentContext(gui);
    ImDrawList list(ImGui::GetDrawListSharedData());
    const bool fonts_ready=ImGui::GetIO().Fonts->TexID!=ImTextureID{};
    if(fonts_ready) {
        list._ResetForNewFrame();list.PushTextureID(ImGui::GetIO().Fonts->TexID);list.PushClipRect(ImVec2(0,0),ImVec2(width,height));
        FrameInfo frame=world_frame(g,cam);
        in_injection=true;draw(&list,width,height,frame,true,false);in_injection=false;
        if(frame_paused)fade(list,0,paused_strength);
        world_counts={frame.spawn_count,frame.trigger_count,frame.player_overlaps};
    }
    ImGui::SetCurrentContext(saved_context);
    world_injected=fonts_ready;
    if(batch.empty()&&list.VtxBuffer.empty())return;
    ID3D11RenderTargetView* old[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};ID3D11DepthStencilView* old_depth=nullptr;
    context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,old,&old_depth);
    auto* scene=frame_trace::scene_depth();  // always drawn under the HUD (user request 2026-10-13)
    // Weapon: anything within 1.5 near planes (the viewmodel reads about 1.015).
    collision_gpu::Occlusion occlusion;occlusion.scene=scene;occlusion.near_plane=scene_near;occlusion.weapon_distance=1.5f*scene_near;
    const nav::Vec3* axes[]={&cam.origin,&cam.forward,&cam.left,&cam.up};float* out[]={occlusion.origin,occlusion.forward,occlusion.left,occlusion.up};
    for(int a=0;a<4;++a) {out[a][0]=axes[a]->x;out[a][1]=axes[a]->y;out[a][2]=axes[a]->z;}
    occlusion.tan_x=cam.tan_x;occlusion.tan_y=cam.tan_y;
    // Only living zombies (box grown for arms and heads, swept over their trail) and
    // the weapon cover collision.
    const auto now=GetTickCount64();
    if(const auto z=zombies::snapshot();z&&now-z->updated<=200)
        for(const auto& marker:z->markers) {
            nav::Vec3 origin{},mins{},maxs{};
            if(occlusion.box_count>=collision_gpu::Occlusion::max_boxes||!zombies::living_bounds(marker,origin,mins,maxs))continue;
            auto& t=trails[marker.number];
            if(t.actor!=marker.actor)t=Trail{marker.actor};
            t.time[t.next]=now;t.lo[t.next]=origin+mins;t.hi[t.next]=origin+maxs;t.next=(t.next+1)%32;
            auto lo=origin+mins,hi=origin+maxs;
            for(unsigned i=0;i<32;++i) {
                if(!t.time[i]||now-t.time[i]>trail_ms)continue;
                lo={std::min(lo.x,t.lo[i].x),std::min(lo.y,t.lo[i].y),std::min(lo.z,t.lo[i].z)};
                hi={std::max(hi.x,t.hi[i].x),std::max(hi.y,t.hi[i].y),std::max(hi.z,t.hi[i].z)};
            }
            auto& b=occlusion.boxes[occlusion.box_count++];
            b[0]=lo.x-12;b[1]=lo.y-12;b[2]=lo.z-4;b[3]=hi.x+12;b[4]=hi.y+12;b[5]=hi.z+12;
        }
    for(auto i=trails.begin();i!=trails.end();) {
        const auto& t=i->second;const auto newest=t.time[(t.next+31)%32];
        if(now-newest>1000)i=trails.erase(i);else ++i;
    }
    if(!batch.empty())collision_gpu::draw(device,context,target,backbuffer_width,backbuffer_height,batch,scene?&occlusion:nullptr);
    if(!list.VtxBuffer.empty()) {
        const float port[4]={batch.viewport_x,batch.viewport_y,batch.viewport_width,batch.viewport_height};
        collision_gpu::draw_list(device,context,target,backbuffer_width,backbuffer_height,list,port,scene?&occlusion:nullptr);
    }
    context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,old,old_depth);
    for(auto& t:old)release(t);release(old_depth);if(scene)scene->Release();
}
// Origins wind puzzle 2 aid. Boxes, arrows and the verdict come from the GSC helper's
// traces (staff_aid.hpp); this only draws fresh samples, at render rate.
// details adds the verdict label and live readout to the boxes, arrows and
// landing ring.
void draw_wind(ImDrawList* list,const nav::Camera& cam,const staff::View& view,float width,float height,float sx,float sy,bool details) {
 const auto now=GetTickCount64();
 auto segment=[&](nav::Vec3 a,nav::Vec3 b,ImU32 color,float thickness) {
  auto p=nav::camera_space(cam,a),q=nav::camera_space(cam,b);
  if(nav::clip_segment(cam,p,q))list->AddLine(screen(nav::project(cam,p),sx,sy),screen(nav::project(cam,q),sx,sy),color,thickness);
 };
 const unsigned edges[][2]={{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
 for(const auto& s:view.smokes) {
  if(!staff::fresh(s.updated,now,staff::smoke_lifetime_ms))continue;
  const auto color=s.cooling?IM_COL32(255,215,60,255):s.solved?IM_COL32(60,230,110,255):IM_COL32(255,70,70,255);
  const auto lo=s.mins,hi=s.maxs;
  const nav::Vec3 corners[]={{lo.x,lo.y,lo.z},{hi.x,lo.y,lo.z},{hi.x,hi.y,lo.z},{lo.x,hi.y,lo.z},{lo.x,lo.y,hi.z},{hi.x,lo.y,hi.z},{hi.x,hi.y,hi.z},{lo.x,hi.y,hi.z}};
  for(const auto& edge:edges)segment(corners[edge[0]],corners[edge[1]],color,2.0f);
  // Arrow from the box centre toward the destination: shots travelling this way pass.
  // Always drawn with the box (user request); only the text is behind details.
  const auto center=(lo+hi)*0.5f,tip=center+s.to_dest*160.0f;
  segment(center,tip,color,2.0f);
  nav::Vec3 side{-s.to_dest.y,s.to_dest.x,0};const float length=std::sqrt(nav::dot(side,side));
  if(length>0.01f) {side=side*(15.0f/length);segment(tip,tip-s.to_dest*30.0f+side,color,2.0f);segment(tip,tip-s.to_dest*30.0f-side,color,2.0f);}
 }
 const auto& aim=view.aim;
 if(!staff::fresh(aim.updated,now,staff::aim_lifetime_ms))return;
 ImU32 color=IM_COL32(170,170,170,255);const char* verdict="miss";
 switch(aim.verdict) {
 case staff::Verdict::fail:color=IM_COL32(255,70,70,255);verdict="fails";break;
 case staff::Verdict::pass:color=IM_COL32(60,230,110,255);verdict="solves";break;
 case staff::Verdict::edge:color=IM_COL32(255,215,60,255);verdict="grazes (uncertain)";break;
 default:break;
 }
 const auto at=nav::camera_space(cam,aim.point);
 if(nav::point_visible(cam,at)) {
  const auto p=screen(nav::project(cam,at),sx,sy);
  // Hollow ring so the impact spot stays visible; black underlay for contrast.
  list->AddCircle(p,14,IM_COL32(0,0,0,255),0,5);list->AddCircle(p,14,color,0,3);
 }
 if(!details)return;
 char label[96]{};
 if(aim.smoke<0)std::snprintf(label,sizeof(label),"Wind: miss");
 else {
  const char* hit=aim.hit==staff::Hit::direct?" (direct)":aim.hit==staff::Hit::splash?" (splash)":"";
  const bool cooling=view.smokes[static_cast<std::size_t>(aim.smoke)].cooling;
  std::snprintf(label,sizeof(label),"Wind: %s smoke %d%s%s",verdict,aim.smoke+1,hit,cooling?" - wait, ignoring hits":"");
 }
 const float scale=height/1080.0f,font_size=19.5f*scale;
 const auto size=fonts::get(font_size)->CalcTextSizeA(font_size,10000,0,label);
 const ImVec2 pos(width*0.5f-size.x*0.5f,height*0.5f+60.0f*scale);
 list->AddText(fonts::get(font_size),font_size,ImVec2(pos.x+scale,pos.y+scale),IM_COL32(0,0,0,200),label);
 list->AddText(fonts::get(font_size),font_size,pos,color,label);
 // Live readout of the verdict's inputs, angles first. Vanilla passes when
 // dot( to_dest, direction ) > cos( script_int ); direction is not normalized, so the
 // effective angle limit is acos( cos( script_int ) / |direction| ), not script_int.
 std::array<std::array<char,112>,7> lines{};unsigned count=0;
 auto line=[&](const char* format,auto... values) {if(count<lines.size())std::snprintf(lines[count++].data(),lines[0].size(),format,values...);};
 float yaw=0,pitch=0;staff::angles(aim.forward,yaw,pitch);
 line("Aim          yaw %6.1f   pitch %5.1f",yaw,pitch);
 const bool smoke_fresh=aim.smoke>=0&&staff::fresh(view.smokes[static_cast<std::size_t>(aim.smoke)].updated,now,staff::smoke_lifetime_ms);
 if(aim.hit!=staff::Hit::none&&smoke_fresh) {
  const auto& smoke=view.smokes[static_cast<std::size_t>(aim.smoke)];
  const float length=std::sqrt(nav::dot(aim.direction,aim.direction));
  staff::angles(aim.direction,yaw,pitch);
  line("Judged       yaw %6.1f   pitch %5.1f   %s |%.1f|",yaw,pitch,aim.hit==staff::Hit::direct?"velocity":"splash",length);
  staff::angles(smoke.to_dest,yaw,pitch);
  line("To dest      yaw %6.1f   pitch %5.1f",yaw,pitch);
  const float angle=staff::angle_between(aim.direction,smoke.to_dest);
  const float nominal=std::acos(std::clamp(aim.threshold,-1.0f,1.0f))*57.2957795f;
  const float limit=length>0?std::acos(std::clamp(aim.threshold/length,-1.0f,1.0f))*57.2957795f:0;
  line("Angle        %5.1f deg   limit %5.2f (nominal %.0f)   margin %+.1f",angle,limit,nominal,limit-angle);
  line("dot %.2f  vs  cos(script_int) %.3f",nav::dot(smoke.to_dest,aim.direction),aim.threshold);
 } else if(aim.verdict==staff::Verdict::edge) line("Within %d units of smoke %d's box: traces and projectile can disagree",2,aim.smoke+1);
 line("World %.1f   brush %.1f   (bullettrace / physicstrace, units)",aim.world_dist,aim.brush_dist);
 if(aim.smoke>=0)line(aim.cooldown_ms>0?"Smoke %d ignoring hits for %.2f s":"Smoke %d accepting hits",aim.smoke+1,aim.cooldown_ms/1000.0f);
 const float detail_size=14.25f*scale,step=detail_size+3*scale;float block=0;
 for(unsigned i=0;i<count;++i)block=std::max(block,fonts::get(detail_size)->CalcTextSizeA(detail_size,10000,0,lines[i].data()).x);
 const ImVec2 top(width*0.5f-block*0.5f,pos.y+font_size+8*scale);
 list->AddRectFilled(ImVec2(top.x-8*scale,top.y-4*scale),ImVec2(top.x+block+8*scale,top.y+count*step+2*scale),IM_COL32(0,0,0,140),4*scale);
 for(unsigned i=0;i<count;++i)list->AddText(fonts::get(detail_size),detail_size,ImVec2(top.x,top.y+i*step),IM_COL32(230,232,236,255),lines[i].data());
}
// Torch number shown to the player for the helper's torch index (0-8). User numbering
// (2026-10-13); the gaps are intentional (11, and no 8 or 10).
int torch_number(int index) {
 constexpr int numbers[]={6,5,7,1,2,3,11,9,4};
 return index>=0&&index<static_cast<int>(std::size(numbers))?numbers[index]:index+1;
}
// Translucent orb shaded like a lit sphere: a dark base over the silhouette, then
// layers shrinking toward a highlight up and to the left that brighten toward white,
// a darker rim and a small specular spot.
void draw_orb(ImDrawList* list,const std::vector<ImVec2>& outline,ImU32 color,bool strong) {
 const int points=static_cast<int>(outline.size());
 ImVec2 centre{};for(const auto& p:outline){centre.x+=p.x;centre.y+=p.y;}centre.x/=points;centre.y/=points;
 float radius=0;for(const auto& p:outline)radius+=std::sqrt((p.x-centre.x)*(p.x-centre.x)+(p.y-centre.y)*(p.y-centre.y));radius/=points;
 const int r=static_cast<int>((color>>IM_COL32_R_SHIFT)&0xFF),g=static_cast<int>((color>>IM_COL32_G_SHIFT)&0xFF),b=static_cast<int>((color>>IM_COL32_B_SHIFT)&0xFF);
 const int base_alpha=strong?60:35;
 list->AddConvexPolyFilled(outline.data(),points,IM_COL32(r*45/100,g*45/100,b*45/100,base_alpha));
 const ImVec2 light(centre.x-radius*0.35f,centre.y-radius*0.4f);
 std::vector<ImVec2> layer(outline.size());
 constexpr int layers=7;
 for(int k=0;k<layers;++k) {
  const float scale=0.92f-0.13f*k,mix=static_cast<float>(k)/layers*0.55f;
  for(int i=0;i<points;++i)layer[static_cast<std::size_t>(i)]=ImVec2(light.x+(outline[static_cast<std::size_t>(i)].x-light.x)*scale,light.y+(outline[static_cast<std::size_t>(i)].y-light.y)*scale);
  const auto shade=IM_COL32(static_cast<int>(r+(255-r)*mix),static_cast<int>(g+(255-g)*mix),static_cast<int>(b+(255-b)*mix),strong?22:14);
  list->AddConvexPolyFilled(layer.data(),points,shade);
 }
 list->AddPolyline(outline.data(),points,IM_COL32(r*35/100,g*35/100,b*35/100,strong?190:120),ImDrawFlags_Closed,2.5f);
 list->AddPolyline(outline.data(),points,(color&~IM_COL32_A_MASK)|IM_COL32(0,0,0,strong?170:110),ImDrawFlags_Closed,1.2f);
 const ImVec2 spot(centre.x-radius*0.42f,centre.y-radius*0.48f);
 list->AddEllipseFilled(spot,ImVec2(radius*0.16f,radius*0.10f),IM_COL32(255,255,255,strong?90:50),-0.6f,16);
}
// Translucent solid sphere: fill its silhouette, the circle where view rays graze it,
// R*R/L in front of the centre with radius R*sqrt(L*L-R*R)/L (L = eye distance).
// Skipped from inside the sphere, where it would cover the whole screen. A fully
// visible silhouette is a shaded orb; one cut by the screen edge keeps a flat fill
// (the orb shading assumes a whole outline). Returns the silhouette's top on screen,
// for a label above it, or -1 when nothing was drawn.
float draw_sphere(ImDrawList* list,const nav::Camera& cam,nav::Vec3 centre,float radius,ImU32 color,bool strong,float sx,float sy) {
 const auto to=centre-cam.origin;const float distance=std::sqrt(nav::dot(to,to));
 if(distance<=radius+1)return -1;
 const auto n=to*(1.0f/distance);
 auto cross=[](nav::Vec3 a,nav::Vec3 b) {return nav::Vec3{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};};
 auto u=cross(n,std::abs(n.z)<0.9f?nav::Vec3{0,0,1}:nav::Vec3{1,0,0});u=u*(1.0f/std::sqrt(nav::dot(u,u)));
 const auto v=cross(n,u);
 const auto centre_ring=centre-n*(radius*radius/distance);const float ring_radius=radius*std::sqrt(distance*distance-radius*radius)/distance;
 constexpr int segments=48;std::vector<nav::Vec3> ring;ring.reserve(segments);
 for(int s=0;s<segments;++s) {
  const float a=6.2831853f*s/segments;
  ring.push_back(nav::camera_space(cam,centre_ring+(u*std::cos(a)+v*std::sin(a))*ring_radius));
 }
 std::vector<ImVec2> pixels;
 for(const auto p:nav::clip_polygon(cam,ring))pixels.push_back(screen(nav::project(cam,p),sx,sy));
 if(pixels.size()<3)return -1;
 const int points=static_cast<int>(pixels.size());
 if(pixels.size()==ring.size())draw_orb(list,pixels,color,strong);
 else {
  list->AddConvexPolyFilled(pixels.data(),points,(color&~IM_COL32_A_MASK)|IM_COL32(0,0,0,strong?55:30));
  list->AddPolyline(pixels.data(),points,(color&~IM_COL32_A_MASK)|IM_COL32(0,0,0,150),ImDrawFlags_Closed,1.5f);
 }
 float top=pixels[0].y;for(const auto& p:pixels)top=std::min(top,p.y);
 return top;
}
// Screen-space occlusion for the torch spheres: a nearer torch's silhouette (a convex
// polygon) hides what lies behind it. Most triangles are wholly outside or wholly
// inside a silhouette and are decided by a point test; only the ones its edge crosses
// are clipped, against each silhouette edge in turn (outside parts kept). No heap use
// per triangle: the per-frame cost was what made overlapping torches lag.
using Shape=std::vector<ImVec2>;
struct Occluder {
 Shape points;float winding=1;ImVec2 lo{},hi{};
 explicit Occluder(Shape s):points(std::move(s)) {
  float sum=0;lo=hi=points[0];
  for(std::size_t i=0;i<points.size();++i) {
   const auto& a=points[i];const auto& b=points[(i+1)%points.size()];sum+=a.x*b.y-b.x*a.y;
   lo.x=std::min(lo.x,a.x);lo.y=std::min(lo.y,a.y);hi.x=std::max(hi.x,a.x);hi.y=std::max(hi.y,a.y);
  }
  winding=sum<0?-1.0f:1.0f;
 }
 float side(std::size_t e,ImVec2 p) const {
  const auto& a=points[e];const auto& b=points[(e+1)%points.size()];
  return winding*((b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x));
 }
 bool contains(ImVec2 p) const {
  if(p.x<lo.x||p.x>hi.x||p.y<lo.y||p.y>hi.y)return false;
  for(std::size_t e=0;e<points.size();++e)if(side(e,p)<0)return false;
  return true;
 }
 bool apart(ImVec2 plo,ImVec2 phi) const {return phi.x<lo.x||plo.x>hi.x||phi.y<lo.y||plo.y>hi.y;}
};
// Small fixed-size polygon for clipping (a triangle cut by a convex polygon stays small).
struct Poly {std::array<ImVec2,16> p{};int n=0;void add(ImVec2 v){if(n<16)p[static_cast<std::size_t>(n++)]=v;}};
// Fills the parts of poly outside every occluder from index first on.
void fill_visible(ImDrawList* list,const Poly& poly,const std::vector<const Occluder*>& occluders,std::size_t first,ImU32 color) {
 ImVec2 plo=poly.p[0],phi=poly.p[0];
 for(int i=1;i<poly.n;++i){const auto v=poly.p[static_cast<std::size_t>(i)];plo.x=std::min(plo.x,v.x);plo.y=std::min(plo.y,v.y);phi.x=std::max(phi.x,v.x);phi.y=std::max(phi.y,v.y);}
 for(std::size_t k=first;k<occluders.size();++k) {
  const auto& o=*occluders[k];
  if(o.apart(plo,phi))continue;
  int inside=0;for(int i=0;i<poly.n;++i)inside+=o.contains(poly.p[static_cast<std::size_t>(i)]);
  if(inside==poly.n)return;  // convex occluder holds every corner: all hidden
  // Clip: split off the part outside each edge; the rest continues to the next edge.
  Poly rest=poly;
  for(std::size_t e=0;e<o.points.size()&&rest.n>=3;++e) {
   float d[16];bool any_out=false;
   for(int i=0;i<rest.n;++i){d[i]=o.side(e,rest.p[static_cast<std::size_t>(i)]);any_out|=d[i]<0;}
   if(!any_out)continue;
   Poly in,out;
   for(int i=0;i<rest.n;++i) {
    const int j=(i+1)%rest.n;const auto a=rest.p[static_cast<std::size_t>(i)],b=rest.p[static_cast<std::size_t>(j)];
    if(d[i]>=0)in.add(a);else out.add(a);
    if((d[i]>=0)!=(d[j]>=0)){const float t=d[i]/(d[i]-d[j]);const ImVec2 x(a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t);in.add(x);out.add(x);}
   }
   if(out.n>=3)fill_visible(list,out,occluders,k+1,color);
   rest=in;
  }
  return;  // what is left is inside this occluder
 }
 list->AddConvexPolyFilled(poly.p.data(),poly.n,color);
}
// Draws the parts of segment a-b outside every occluder from index first on.
void line_visible(ImDrawList* list,ImVec2 a,ImVec2 b,const std::vector<const Occluder*>& occluders,std::size_t first,ImU32 color,float thickness) {
 const ImVec2 plo(std::min(a.x,b.x),std::min(a.y,b.y)),phi(std::max(a.x,b.x),std::max(a.y,b.y));
 for(std::size_t k=first;k<occluders.size();++k) {
  const auto& o=*occluders[k];
  if(o.apart(plo,phi))continue;
  float t0=0,t1=1;bool outside=false;
  for(std::size_t e=0;e<o.points.size()&&!outside;++e) {
   const float da=o.side(e,a),db=o.side(e,b);
   if(da<0&&db<0)outside=true;
   else if(da<0)t0=std::max(t0,da/(da-db));
   else if(db<0)t1=std::min(t1,da/(da-db));
  }
  if(outside||t0>=t1)continue;
  auto at=[&](float t){return ImVec2(a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t);};
  if(t0>0)line_visible(list,a,at(t0),occluders,k+1,color,thickness);
  if(t1<1)line_visible(list,at(t1),b,occluders,k+1,color,thickness);
  return;
 }
 list->AddLine(a,b,color,thickness);
}
// Silhouette of a sphere on screen (see draw_sphere); empty from inside the sphere.
Shape sphere_outline(const nav::Camera& cam,nav::Vec3 centre,float radius,float sx,float sy) {
 const auto to=centre-cam.origin;const float distance=std::sqrt(nav::dot(to,to));
 if(distance<=radius+1)return {};
 const auto n=to*(1.0f/distance);
 auto u=area::cross(n,std::abs(n.z)<0.9f?nav::Vec3{0,0,1}:nav::Vec3{1,0,0});u=u*(1.0f/std::sqrt(nav::dot(u,u)));
 const auto v=area::cross(n,u);
 const auto centre_ring=centre-n*(radius*radius/distance);const float ring_radius=radius*std::sqrt(distance*distance-radius*radius)/distance;
 constexpr int segments=64;std::vector<nav::Vec3> ring;ring.reserve(segments);
 for(int s=0;s<segments;++s){const float a=6.2831853f*s/segments;ring.push_back(nav::camera_space(cam,centre_ring+(u*std::cos(a)+v*std::sin(a))*ring_radius));}
 Shape pixels;
 for(const auto p:nav::clip_polygon(cam,ring))pixels.push_back(screen(nav::project(cam,p),sx,sy));
 if(pixels.size()<3)return {};
 return pixels;
}
// Torch sphere (user request 2026-10-20): the three axis great circles drawn in full and
// the eight sections between them filled with flat translucent colour, alternating
// slightly in brightness. Far side first, near side over it, so the overlap reads as
// depth. Parts covered by a nearer torch's silhouette (occluders) are not drawn.
void draw_octant_sphere(ImDrawList* list,const nav::Camera& cam,nav::Vec3 centre,float radius,ImU32 color,bool strong,
                        const Shape& outline,const std::vector<const Occluder*>& occluders,float sx,float sy) {
 if(outline.empty())return;
 const int r=static_cast<int>((color>>IM_COL32_R_SHIFT)&0xFF),g=static_cast<int>((color>>IM_COL32_G_SHIFT)&0xFF),b=static_cast<int>((color>>IM_COL32_B_SHIFT)&0xFF);
 auto tint=[&](float k,int alpha){return IM_COL32(static_cast<int>(r*k),static_cast<int>(g*k),static_cast<int>(b*k),alpha);};
 auto facing=[&](nav::Vec3 direction){return nav::dot(direction,cam.origin-(centre+direction*radius))>0;};
 auto pixel=[&](nav::Vec3 direction,ImVec2& out) {
  const auto p=nav::camera_space(cam,centre+direction*radius);
  if(p.z<1)return false;
  out=screen(nav::project(cam,p),sx,sy);return true;
 };
 auto fill=[&](const Poly& poly,ImU32 c){fill_visible(list,poly,occluders,0,c);};
 auto line=[&](ImVec2 a,ImVec2 c2,ImU32 c,float thickness){line_visible(list,a,c2,occluders,0,c,thickness);};
 // Triangles share exact edges; anti-aliased fringes would show as seams.
 const auto flags=list->Flags;list->Flags&=~ImDrawListFlags_AntiAliasedFill;
 constexpr int steps=6;
 for(int pass=0;pass<2;++pass) {
  const bool front=pass==1;
  for(int octant=0;octant<8;++octant) {
   const nav::Vec3 a{octant&1?radius:-radius,0,0},bb{0,octant&2?radius:-radius,0},c{0,0,octant&4?radius:-radius};
   const bool light=((octant&1)^((octant>>1)&1)^((octant>>2)&1))!=0;
   const auto shade=tint(light?1.0f:0.72f,front?(strong?46:28):(strong?26:16));
   auto point=[&](int i,int j){auto p=a*static_cast<float>(steps-i-j)+bb*static_cast<float>(i)+c*static_cast<float>(j);return p*(1.0f/std::sqrt(nav::dot(p,p)));};
   auto triangle=[&](nav::Vec3 p,nav::Vec3 q,nav::Vec3 s) {
    auto mid=p+q+s;mid=mid*(1.0f/std::sqrt(nav::dot(mid,mid)));
    if(facing(mid)!=front)return;
    Poly poly;poly.n=3;
    if(!pixel(p,poly.p[0])||!pixel(q,poly.p[1])||!pixel(s,poly.p[2]))return;
    fill(poly,shade);
   };
   for(int i=0;i<steps;++i)for(int j=0;i+j<steps;++j) {
    triangle(point(i,j),point(i+1,j),point(i,j+1));
    if(i+j+1<steps)triangle(point(i+1,j),point(i+1,j+1),point(i,j+1));
   }
  }
  list->Flags=flags;
  // Great circles in the three axis planes: thin and faint behind, bright in front.
  constexpr int segments=72;
  for(int axis=0;axis<3;++axis)for(int s=0;s<segments;++s) {
   auto dir=[&](int k) {
    const float t=6.2831853f*k/segments,x=std::cos(t),y=std::sin(t);
    return axis==0?nav::Vec3{0,x,y}:axis==1?nav::Vec3{x,0,y}:nav::Vec3{x,y,0};
   };
   const auto p=dir(s),q=dir(s+1);
   if(facing((p+q)*0.5f)!=front)continue;
   ImVec2 pa,pb;if(!pixel(p,pa)||!pixel(q,pb))continue;
   line(pa,pb,front?tint(1.0f,strong?230:170):tint(0.8f,strong?90:60),front?1.6f:1.0f);
  }
  if(!front)list->Flags&=~ImDrawListFlags_AntiAliasedFill;
 }
 list->Flags=flags;
 for(std::size_t i=0;i<outline.size();++i)line(outline[i],outline[(i+1)%outline.size()],tint(1.0f,strong?200:130),1.4f);
}
// Origins fire puzzle 2 aid: torch spheres and the three predicted explosions, from
// the GSC helper's traces (staff_aid.hpp); fresh samples only, drawn at render rate.
// Torch labels (torch_number) are always shown; details adds the summary line and live
// readout to the landing rings.
void draw_fire(ImDrawList* list,const nav::Camera& cam,const staff::View& view,float width,float height,float sx,float sy,bool details) {
 const auto now=GetTickCount64();
 auto torch_color=[](const staff::Torch& t) {
  if(t.correct)return t.lit?IM_COL32(60,230,110,255):IM_COL32(255,160,40,255);
  return t.lit?IM_COL32(255,70,70,255):IM_COL32(150,150,150,150);
 };
 unsigned correct=0,correct_lit=0,wrong_lit=0;
 // Far to near, each torch hidden where a nearer torch's silhouette covers it.
 std::vector<std::size_t> order;std::array<Shape,9> outlines{};std::array<float,9> distances{};std::array<std::optional<Occluder>,9> shapes{};
 for(std::size_t i=0;i<view.torches.size();++i) {
  const auto& t=view.torches[i];
  if(!staff::fresh(t.updated,now,staff::smoke_lifetime_ms))continue;
  const auto to=t.origin-cam.origin;distances[i]=nav::dot(to,to);
  outlines[i]=sphere_outline(cam,t.origin,staff::torch_radius,sx,sy);
  if(!outlines[i].empty())shapes[i].emplace(outlines[i]);
  order.push_back(i);
 }
 std::sort(order.begin(),order.end(),[&](std::size_t a,std::size_t b){return distances[a]>distances[b];});
 for(std::size_t k=0;k<order.size();++k) {
  const std::size_t i=order[k];
  const auto& t=view.torches[i];
  correct+=t.correct;correct_lit+=t.correct&&t.lit;wrong_lit+=!t.correct&&t.lit;
  const auto color=torch_color(t);
  std::vector<const Occluder*> occluders;
  for(std::size_t m=k+1;m<order.size();++m)if(!outlines[order[m]].empty())occluders.push_back(&*shapes[order[m]]);
  draw_octant_sphere(list,cam,t.origin,staff::torch_radius,color,t.correct||t.lit,outlines[i],occluders,sx,sy);
  const auto centre=nav::camera_space(cam,t.origin);
  // Small solid ball at the centre (the actual test point) for depth perspective;
  // it shrinks with distance like a 4-unit sphere would.
  bool hidden=false;
  if(nav::point_visible(cam,centre))for(const auto* o:occluders)if(o->contains(screen(nav::project(cam,centre),sx,sy)))hidden=true;
  if(nav::point_visible(cam,centre)&&!hidden) {
   const auto p=screen(nav::project(cam,centre),sx,sy);
   const float pixels=std::clamp(4.0f/(centre.z*cam.tan_y)*cam.height*0.5f*sy,2.0f,10.0f);
   list->AddCircleFilled(p,pixels+1,IM_COL32(0,0,0,200));list->AddCircleFilled(p,pixels,(color&~IM_COL32_A_MASK)|IM_COL32(0,0,0,255));
  }
  // Label hovering over the sphere's top point (centre + radius up), the same anchor near,
  // far and from inside (user request 2026-10-20), on a dark plate over bright walls.
  const auto top=nav::camera_space(cam,t.origin+nav::Vec3{0,0,staff::torch_radius});
  if(nav::point_visible(cam,top)) {
   char text[48]{};
   const int number=torch_number(static_cast<int>(i));
   if(t.lit)std::snprintf(text,sizeof(text),"T%d%s %.0fs",number,t.correct?" correct":"",t.lit_left_ms/1000.0f);
   else std::snprintf(text,sizeof(text),"T%d%s",number,t.correct?" correct":"");
   const auto p=screen(nav::project(cam,top),sx,sy);
   const float scale=height/1080.0f,font_size=std::max(11.0f,15.0f*scale);
   const auto size=fonts::get(font_size)->CalcTextSizeA(font_size,10000,0,text);
   const float y=p.y-size.y-8*scale;
   const ImVec2 at(p.x-size.x*0.5f,y);
   list->AddRectFilled(ImVec2(at.x-5*scale,at.y-2*scale),ImVec2(at.x+size.x+5*scale,at.y+size.y+2*scale),IM_COL32(0,0,0,150),4*scale);
   list->AddText(fonts::get(font_size),font_size,ImVec2(at.x+1,at.y+1),IM_COL32(0,0,0,220),text);
   list->AddText(fonts::get(font_size),font_size,at,(color&~IM_COL32_A_MASK)|IM_COL32(0,0,0,255),text);
  }
 }
 const auto& fire=view.fire;
 if(!staff::fresh(fire.updated,now,staff::aim_lifetime_ms))return;
 const char* names[]={"Main","Left","Right"};
 std::array<std::array<char,112>,8> lines{};unsigned count=0;
 auto line=[&](const char* format,auto... values) {if(count<lines.size())std::snprintf(lines[count++].data(),lines[0].size(),format,values...);};
 float yaw=0,pitch=0;staff::angles(fire.forward,yaw,pitch);
 line("Aim          yaw %6.1f   pitch %5.1f",yaw,pitch);
 char label[128]="Fire:";
 for(unsigned shot=0;shot<3;++shot) {
  if(shot>0&&!fire.sides)continue;
  const int t=fire.torch[shot];
  const bool torch_ok=t>=0&&staff::fresh(view.torches[static_cast<std::size_t>(t)].updated,now,staff::smoke_lifetime_ms);
  ImU32 color=IM_COL32(170,170,170,255);const char* effect="nothing";
  if(torch_ok) {
   const auto& torch=view.torches[static_cast<std::size_t>(t)];
   if(torch.rearm_ms>0) {color=IM_COL32(255,215,60,255);effect="ignored";}
   else if(!torch.correct) {color=IM_COL32(255,70,70,255);effect="WRONG";}
   else {color=IM_COL32(60,230,110,255);effect=torch.lit?"relights":"lights";}
  }
  const auto at=nav::camera_space(cam,fire.points[shot]);
  if(nav::point_visible(cam,at)) {
   const auto p=screen(nav::project(cam,at),sx,sy);const float r=shot==0?14.0f:10.0f;
   list->AddCircle(p,r,IM_COL32(0,0,0,255),0,5);list->AddCircle(p,r,color,0,3);
  }
  const auto d=fire.points[shot]-fire.muzzle;const float distance=std::sqrt(nav::dot(d,d));
  char part[40]{};
  if(torch_ok)std::snprintf(part,sizeof(part)," %s T%d %s",names[shot],torch_number(t),effect);
  else std::snprintf(part,sizeof(part)," %s -",names[shot]);
  strncat_s(label,part,_TRUNCATE);
  // Nearest torch distance against the 64-unit radius, for every shot.
  float nearest=1e9f;int nearest_index=-1;
  for(std::size_t i=0;i<view.torches.size();++i) {
   if(!staff::fresh(view.torches[i].updated,now,staff::smoke_lifetime_ms))continue;
   const auto e=fire.points[shot]-view.torches[i].origin;const float dist=std::sqrt(nav::dot(e,e));
   if(dist<nearest) {nearest=dist;nearest_index=static_cast<int>(i);}
  }
  if(nearest_index>=0)line("%-5s %s   range %5.0f   nearest T%d at %5.1f (hit <= %.0f)%s",names[shot],shot==0?"  0 deg":shot==1?"-29 deg":"+29 deg",distance,torch_number(nearest_index),nearest,staff::torch_radius,distance>350?"   beyond measured range":"");
 }
 if(!fire.sides)line("Side shots: none (something within 100 units ahead)");
 else line("Side shots fire 0.2 s and 0.4 s later, from your aim at that moment");
 line("Correct torches lit %u/%u   wrong lit %u",correct_lit,correct,wrong_lit);
 if(!details)return;
 const float scale=height/1080.0f,font_size=19.5f*scale;
 const auto size=fonts::get(font_size)->CalcTextSizeA(font_size,10000,0,label);
 const ImVec2 pos(width*0.5f-size.x*0.5f,height*0.5f+60.0f*scale);
 list->AddText(fonts::get(font_size),font_size,ImVec2(pos.x+scale,pos.y+scale),IM_COL32(0,0,0,200),label);
 list->AddText(fonts::get(font_size),font_size,pos,IM_COL32(240,240,240,255),label);
 const float detail_size=14.25f*scale,step=detail_size+3*scale;float block=0;
 for(unsigned i=0;i<count;++i)block=std::max(block,fonts::get(detail_size)->CalcTextSizeA(detail_size,10000,0,lines[i].data()).x);
 const ImVec2 top(width*0.5f-block*0.5f,pos.y+font_size+8*scale);
 list->AddRectFilled(ImVec2(top.x-8*scale,top.y-4*scale),ImVec2(top.x+block+8*scale,top.y+count*step+2*scale),IM_COL32(0,0,0,140),4*scale);
 for(unsigned i=0;i<count;++i)list->AddText(fonts::get(detail_size),detail_size,ImVec2(top.x,top.y+i*step),IM_COL32(230,232,236,255),lines[i].data());
}
// Mob of the Dead blue-skull aid (mob_aid.hpp): the 100-unit catch sphere of each
// skull (green when the predicted check catches it, cyan in its 1.6 s reveal), the
// skull's bounds (the lightning-hands target, approximate), the predicted tomahawk arc
// and check ring while the tomahawk is owned, the last real flight and check for a
// few seconds, and in Afterlife the shock boxes with where to stand to reach them.
void draw_mob(ImDrawList* list,const nav::Camera& cam,const mob::View& view,float height,float sx,float sy) {
 const auto now=GetTickCount64();
 const float scale=height/1080.0f,font_size=std::max(11.0f,15.0f*scale);
 auto segment=[&](nav::Vec3 a,nav::Vec3 b,ImU32 color,float thickness) {
  auto p=nav::camera_space(cam,a),q=nav::camera_space(cam,b);
  if(nav::clip_segment(cam,p,q))list->AddLine(screen(nav::project(cam,p),sx,sy),screen(nav::project(cam,q),sx,sy),color,thickness);
 };
 auto box=[&](nav::Vec3 lo,nav::Vec3 hi,ImU32 color) {
  const nav::Vec3 c[]={{lo.x,lo.y,lo.z},{hi.x,lo.y,lo.z},{hi.x,hi.y,lo.z},{lo.x,hi.y,lo.z},{lo.x,lo.y,hi.z},{hi.x,lo.y,hi.z},{hi.x,hi.y,hi.z},{lo.x,hi.y,hi.z}};
  const unsigned edges[][2]={{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
  for(const auto& e:edges)segment(c[e[0]],c[e[1]],color,1.5f);
 };
 // Hollow ring on screen around a world point, black underlay for contrast.
 auto ring=[&](nav::Vec3 world,float radius,ImU32 color) {
  const auto at=nav::camera_space(cam,world);if(!nav::point_visible(cam,at))return;
  const auto p=screen(nav::project(cam,at),sx,sy);
  list->AddCircle(p,radius,IM_COL32(0,0,0,255),0,5);list->AddCircle(p,radius,color,0,3);
 };
 // Text centred above a screen y (or above the point), on a dark plate.
 auto caption=[&](nav::Vec3 world,float top,ImU32 color,const char* text) {
  const auto at=nav::camera_space(cam,world);if(!nav::point_visible(cam,at))return;
  const auto p=screen(nav::project(cam,at),sx,sy);
  const auto size=fonts::get(font_size)->CalcTextSizeA(font_size,10000,0,text);
  float y=top>=0?top-size.y-8*scale:p.y-size.y-12*scale;if(y<4)y=p.y-size.y-12*scale;
  const ImVec2 pos(p.x-size.x*0.5f,y);
  list->AddRectFilled(ImVec2(pos.x-5*scale,pos.y-2*scale),ImVec2(pos.x+size.x+5*scale,pos.y+size.y+2*scale),IM_COL32(0,0,0,150),4*scale);
  list->AddText(fonts::get(font_size),font_size,ImVec2(pos.x+1,pos.y+1),IM_COL32(0,0,0,220),text);
  list->AddText(fonts::get(font_size),font_size,pos,color,text);
 };
 const bool live=mob::fresh(view.state.updated,now,mob::live_ms);
 if(mob_skulls) {
  const bool aiming=live&&view.state.tomahawk&&!view.state.afterlife&&mob::fresh(view.aim.updated,now,mob::live_ms);
  const bool flying=mob::fresh(view.flight.updated,now,mob::flight_ms);
  for(std::size_t i=0;i<view.skulls.size();++i) {
   const auto& s=view.skulls[i];
   if(!mob::fresh(s.updated,now,mob::skull_ms))continue;
   const bool catching=aiming&&view.aim.skull==static_cast<int>(i);
   const bool shown=s.shown_ms>=0;
   const ImU32 color=catching?IM_COL32(60,230,110,255):shown?IM_COL32(110,235,255,255):IM_COL32(80,140,255,255);
   const float top=draw_sphere(list,cam,s.origin,mob::catch_radius,color,catching||shown,sx,sy);
   box(s.mins,s.maxs,IM_COL32(150,210,255,220));
   char text[48]{};
   if(shown)std::snprintf(text,sizeof(text),"Skull %u  shown %.1fs",static_cast<unsigned>(i+1),s.shown_ms/1000.0f);
   else std::snprintf(text,sizeof(text),"Skull %u",static_cast<unsigned>(i+1));
   caption(s.origin,top,color,text);
  }
  if(aiming) {
   const auto& a=view.aim;
   const ImU32 color=a.skull>=0?IM_COL32(60,230,110,230):IM_COL32(235,235,240,170);
   constexpr int steps=22;
   for(int k=0;k<steps;++k)segment(mob::arc(a,a.stop*k/steps),mob::arc(a,a.stop*(k+1)/steps),color,2.0f);
   const ImU32 ring_color=a.skull>=0?IM_COL32(60,230,110,255):a.impact?IM_COL32(255,160,40,255):IM_COL32(170,170,170,255);
   ring(a.check,14,ring_color);
   // 3D distance from the check point to the nearest skull centre against the catch
   // radius: on screen a point can sit inside a skull's sphere while being far in
   // front of or behind it (user report 2026-10-14: orange ring inside a blue orb).
   float nearest=-1;
   for(const auto& s:view.skulls) {
    if(!mob::fresh(s.updated,now,mob::skull_ms))continue;
    const auto d=s.origin-a.check;const float length=std::sqrt(nav::dot(d,d));
    if(nearest<0||length<nearest)nearest=length;
   }
   if(nearest>=0) {
    char text[48]{};
    std::snprintf(text,sizeof(text),"%s%.0f / %.0f",a.impact?"wall ":"",nearest,mob::catch_radius);
    caption(a.check,-1,ring_color,text);
   }
  }
  if(flying) {
   const auto& f=view.flight;
   for(unsigned k=1;k<f.count;++k)segment(f.points[k-1],f.points[k],IM_COL32(255,215,60,220),2.0f);
   if(f.done) {
    const ImU32 color=f.skull>=0?IM_COL32(60,230,110,255):IM_COL32(255,70,70,255);
    ring(f.check,10,color);
    char text[48]{};
    if(f.skull>=0)std::snprintf(text,sizeof(text),"Caught skull %d",f.skull+1);
    else std::snprintf(text,sizeof(text),"Checked here: no skull");
    caption(f.check,-1,color,text);
   }
  }
 }
 if(mob_boxes&&live&&view.state.afterlife) {
  const auto player=view.state.player;
  // Declutter (user screenshot 2026-10-14): outlines only for boxes within 1000 units,
  // and the range shape only for the nearest box once within 600.
  constexpr float outline_distance=1000,range_distance=600;
  const mob::Box* nearest=nullptr;float nearest_distance=0;
  for(const auto& b:view.boxes) {
   if(!mob::fresh(b.updated,now,mob::box_ms))continue;
   const auto d=player-b.origin;const float distance=std::sqrt(nav::dot(d,d));
   if(!nearest||distance<nearest_distance) {nearest=&b;nearest_distance=distance;}
  }
  for(const auto& b:view.boxes) {
   if(!mob::fresh(b.updated,now,mob::box_ms))continue;
   const auto to=player-b.origin;
   if(nav::dot(to,to)>outline_distance*outline_distance)continue;
   box(b.mins,b.maxs,&b==nearest?IM_COL32(255,200,80,230):IM_COL32(255,200,80,110));
   if(&b!=nearest||nearest_distance>range_distance)continue;
   // The activation range, static around the box (user request 2026-10-14; the
   // earlier ring followed the player's height). Door boxes test distance2d, an
   // infinite column: a hollow cylinder, drawn 128 above and below the box. The other
   // boxes test 3D distance: a wire sphere (horizontal and two vertical great circles).
   const auto d=player-b.origin;const float reach=mob::interact_range;
   const bool inside=b.planar?d.x*d.x+d.y*d.y<reach*reach:nav::dot(d,d)<reach*reach;
   const ImU32 color=inside?IM_COL32(60,230,110,200):IM_COL32(200,200,210,120);
   constexpr int segments=48;
   auto circle=[&](auto point) {
    for(int k=0;k<segments;++k)segment(point(6.2831853f*k/segments),point(6.2831853f*(k+1)/segments),color,1.5f);
   };
   const auto o=b.origin;
   if(b.planar) {
    constexpr float half_height=128;
    for(const float z:{o.z-half_height,o.z+half_height})
     circle([&](float a) {return nav::Vec3{o.x+reach*std::cos(a),o.y+reach*std::sin(a),z};});
    for(int k=0;k<8;++k) {
     const float a=6.2831853f*k/8;
     segment(nav::Vec3{o.x+reach*std::cos(a),o.y+reach*std::sin(a),o.z-half_height},
             nav::Vec3{o.x+reach*std::cos(a),o.y+reach*std::sin(a),o.z+half_height},color,1.5f);
    }
   } else {
    circle([&](float a) {return nav::Vec3{o.x+reach*std::cos(a),o.y+reach*std::sin(a),o.z};});
    circle([&](float a) {return nav::Vec3{o.x+reach*std::cos(a),o.y,o.z+reach*std::sin(a)};});
    circle([&](float a) {return nav::Vec3{o.x,o.y+reach*std::cos(a),o.z+reach*std::sin(a)};});
   }
  }
 }
}
// world: the in-world drawings (NAV, spawns, triggers, aids, zombie markers); since
// 2026-10-14 normally drawn by the injection under the weapon and HUD. collision_part:
// the Present-time collision fallback. Present runs both only when the injection did not.
void draw(ImDrawList* list,float width,float height,FrameInfo& frame,bool world,bool collision_part) {
    const auto& g=frame.graph;const auto& cam=frame.camera;

    unsigned points=0,edges=0,label_count=0,zombie_count=0;
    // In-world labels: TTF text with a 1-pixel shadow, a little larger at high resolution.
    const float label_size=std::max(11.0f,11.25f*height/1080.0f);  // 25% smaller (user, 2026-10-13)
    auto label=[&](ImVec2 at,ImU32 color,const char* text) {
        list->AddText(fonts::get(label_size),label_size,ImVec2(at.x+1,at.y+1),IM_COL32(0,0,0,200),text);
        list->AddText(fonts::get(label_size),label_size,at,color,text);
    };
    // Spawn-site label under its symbol, starting at the symbol's left edge.
    auto under_symbol=[&](ImVec2 p,float radius,ImU32 color,const char* text) {label(ImVec2(p.x-radius,p.y+radius+2),color,text);};
    if(frame.camera_valid) {
        // Refdef viewport is in render pixels. Scale the complete viewport to
        // the swap-chain backbuffer, including its offset.
        const float sx=width/static_cast<float>(cam.x+cam.width);
        const float sy=height/static_cast<float>(cam.y+cam.height);
        list->PushClipRect(ImVec2(cam.x*sx,cam.y*sy),ImVec2(width,height),true);
        std::vector<nav::Vec3> projected(g->nodes.size());
        std::vector<unsigned char> keep(g->nodes.size());
        for(std::size_t i=0;i<g->nodes.size();++i) {
            const auto& n=g->nodes[i];
            keep[i]=static_cast<unsigned char>(world&&nodes&&nav::within_draw_range(n.origin,cam.origin,range,vertical(range)));
            if(keep[i])projected[i]=nav::camera_space(cam,n.origin);
        }
        // The GPU pass drew links and node symbols under zombies; only ID labels remain.
        const bool gpu_nav=nav_injected&&(in_injection||frame_trace::injected_last_frame());
        if(world&&links&&!gpu_nav)for(std::size_t i=0;i<g->nodes.size();++i) {
            if(!keep[i])continue;
            for(const auto dest:g->nodes[i].links) {
                if(!keep[dest])continue;
                // Draw a reciprocal pair once unless direction arrows are on.
                // One-way edges are preserved regardless of index ordering.
                const bool crossing=nav::traversal_crossing(g->nodes[i].type,g->nodes[dest].type);
                const bool opposite_crossing=nav::traversal_crossing(g->nodes[dest].type,g->nodes[i].type);
                if(dest<i&&!(arrows&&(crossing||opposite_crossing))&&std::find(g->nodes[dest].links.begin(),g->nodes[dest].links.end(),static_cast<std::uint16_t>(i))!=g->nodes[dest].links.end())continue;
                auto a=projected[i],b=projected[dest];
                if(!nav::clip_segment(cam,a,b))continue;
                const auto p=screen(nav::project(cam,a),sx,sy),q=screen(nav::project(cam,b),sx,sy);
                if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(q.x)||!std::isfinite(q.y))continue;
                const ImU32 link_color=IM_COL32(90,205,229,150);
                list->AddLine(p,q,link_color,crossing?1.5f:1);++edges;
                if(arrows&&crossing) {
                    const float dx=q.x-p.x,dy=q.y-p.y,len=std::sqrt(dx*dx+dy*dy);
                    // Anchor in world space before clipping or projection.
                    // A screen fraction slides as perspective/clipped ends vary.
                    const auto from=g->nodes[i].origin,to=g->nodes[dest].origin;
                    if(len>0.001f) {
                        const float ux=dx/len,uy=dy/len;
                        for(const float fraction:{0.25f,0.50f,0.75f}) {
                            const auto anchor=nav::camera_space(cam,from+(to-from)*fraction);
                            if(!nav::point_visible(cam,anchor))continue;
                            const auto tip=screen(nav::project(cam,anchor),sx,sy);
                            list->AddLine(tip,ImVec2(tip.x-ux*6-uy*3,tip.y-uy*6+ux*3),link_color,2);
                            list->AddLine(tip,ImVec2(tip.x-ux*6+uy*3,tip.y-uy*6-ux*3),link_color,2);
                        }
                    }
                }
            }
        }
        for(std::size_t i=0;i<g->nodes.size();++i) {
            if(!keep[i]||!nav::point_visible(cam,projected[i]))continue;
            const auto p=screen(nav::project(cam,projected[i]),sx,sy);
            const auto type=g->nodes[i].type;
            const ImU32 color=IM_COL32(174,150,255,235);
            if(gpu_nav) {}
            else if(type==17)list->AddQuadFilled(ImVec2(p.x,p.y-6.75f),ImVec2(p.x+6.75f,p.y),ImVec2(p.x,p.y+6.75f),ImVec2(p.x-6.75f,p.y),color);
            else if(type==18)list->AddRect(ImVec2(p.x-6,p.y-6),ImVec2(p.x+6,p.y+6),color,0,0,2.25f);
            else list->AddCircleFilled(p,2.5f,color,8);
            ++points;
            if(labels&&label_count<120&&projected[i].z<900) {
                char id[48]{};
                if(type==1)std::snprintf(id,sizeof(id),"%u",static_cast<unsigned>(i));
                else if(type==17||type==18)std::snprintf(id,sizeof(id),"%u %s",static_cast<unsigned>(i),type==17?"B":"E");
                else std::snprintf(id,sizeof(id),"%u t%u",static_cast<unsigned>(i),type);
                label(ImVec2(p.x+((type==17||type==18)?9:5),p.y-label_size*0.5f),color,id);++label_count;
            }
        }
        list->PopClipRect();
        list->PushClipRect(ImVec2(cam.x*sx,cam.y*sy),ImVec2(width,height),true);
        if(world&&spawn_sites&&frame.spawn_valid) {
            constexpr float symbol_scale=0.67f;
            const auto drawing_sites=spawns::drawing_sites(frame.spawn_snapshot->sites);
            // Distinct configured objects can share an exact origin. Draw
            // eligible sites last so a red alternative cannot cover green.
            for(int active_pass=0;active_pass<2;++active_pass) {
            for(auto site:drawing_sites) {
                site.types=spawns::visible_types(site.types,special_spawn_sites);
                if(!site.types)continue;
                if(site.active!=(active_pass==1))continue;
                if(active_spawns_only&&!site.active)continue;
                if(!nav::within_draw_range(site.origin,cam.origin,spawn_range,vertical(spawn_range)))continue;
                const auto relative=nav::camera_space(cam,site.origin);
                if(!nav::point_visible(cam,relative))continue;
                const auto p=screen(nav::project(cam,relative),sx,sy);
                const ImU32 color=site.active?IM_COL32(65,235,115,255):IM_COL32(150,155,165,255);
                if((site.types&5)==5) {
                    list->AddCircleFilled(p,11*symbol_scale,IM_COL32(0,0,0,255),24);
                    list->AddCircleFilled(p,8.5f*symbol_scale,color,24);
                    const ImVec2 top(p.x,p.y-5*symbol_scale),east(p.x+5*symbol_scale,p.y),bottom(p.x,p.y+5*symbol_scale),west(p.x-5*symbol_scale,p.y);
                    list->AddQuad(top,east,bottom,west,IM_COL32(0,0,0,255),2*symbol_scale);
                    list->AddQuadFilled(top,east,bottom,west,IM_COL32(195,145,255,255));
                    if(spawn_labels) {const auto text=spawns::site_label(site.types);under_symbol(p,11*symbol_scale,IM_COL32(230,205,255,255),text.c_str());}
                } else if((site.types&32)&&!(site.types&16)) {
                    const float radius=7*symbol_scale;
                    const ImVec2 upper(p.x-radius,p.y-radius),lower(p.x+radius,p.y+radius);
                    list->AddRectFilled(upper,lower,IM_COL32(195,145,255,255));
                    list->AddRect(upper,lower,IM_COL32(0,0,0,255),0,0,3*symbol_scale);
                    const auto site_label=spawns::site_label(site.types);
                    if(spawn_labels)under_symbol(p,radius,IM_COL32(230,205,255,255),site_label.c_str());
                    if(site.types&1)list->AddCircleFilled(p,3*symbol_scale,color,16);
                } else if(site.types&126) {
                    const ImVec2 top(p.x,p.y-9*symbol_scale),east(p.x+9*symbol_scale,p.y),bottom(p.x,p.y+9*symbol_scale),west(p.x-9*symbol_scale,p.y);
                    list->AddQuad(top,east,bottom,west,IM_COL32(0,0,0,255),3*symbol_scale);
                    // Lavender identifies a registered special site, not active chooser membership.
                    list->AddQuadFilled(top,east,bottom,west,IM_COL32(195,145,255,255));
                    const auto site_label=spawns::site_label(site.types);
                    if(spawn_labels)under_symbol(p,9*symbol_scale,IM_COL32(230,205,255,255),site_label.c_str());
                    if(site.types&1)list->AddCircleFilled(p,3*symbol_scale,color,16);
                } else {
                    list->AddCircleFilled(p,9*symbol_scale,IM_COL32(0,0,0,255),24);
                    list->AddCircleFilled(p,6.5f*symbol_scale,color,24);
                    if(spawn_labels)under_symbol(p,9*symbol_scale,IM_COL32(240,240,245,255),"Zombie");
                }
                ++frame.spawn_count;
            }
            }
        }
        if(world&&(trigger_positions||playable_areas)&&frame.trigger_snapshot&&triggers::fresh(*frame.trigger_snapshot,*g,GetTickCount64())) {
            auto in_range=[&](const triggers::Marker& marker,nav::Vec3 origin,nav::Vec3 angles) {
                if(marker.shape=="Cylinder")return triggers::cylinder_range_overlap(origin,marker.maxs.x,marker.maxs.z,cam.origin,trigger_range,vertical(trigger_range));
                for(const auto& mesh:marker.meshes)if(triggers::range_overlap(mesh,cam.origin,trigger_range,vertical(trigger_range),[&](nav::Vec3 p){return origin+zombies::rotate(p,angles);}))return true;
                return false;
            };
            auto safe_role=[](triggers::Region role) {return role==triggers::Region::enabled_player||role==triggers::Region::life;};
            // Playable area: the merged surface of every enabled and life volume
            // (playable_surface), cut to the draw radius. Nearby surface reads strongest;
            // far surface fades to a quarter.
            if(playable_areas) {
                const auto& surface=playable_surface(frame.trigger_snapshot);
                const auto c=cam.origin;const float hr=trigger_range,vr=vertical(trigger_range);
                auto close_by=[&](nav::Vec3 lo,nav::Vec3 hi) {return hi.x>=c.x-hr&&lo.x<=c.x+hr&&hi.y>=c.y-hr&&lo.y<=c.y+hr&&hi.z>=c.z-vr&&lo.z<=c.z+vr;};
                auto faded=[&](ImU32 color,nav::Vec3 at) {
                    const auto to=at-c;const float fade=std::clamp(1.0f-std::sqrt(nav::dot(to,to))/trigger_range,0.25f,1.0f);
                    return (color&~IM_COL32_A_MASK)|(static_cast<ImU32>(static_cast<float>((color>>IM_COL32_A_SHIFT)&0xFF)*fade)<<IM_COL32_A_SHIFT);
                };
                for(const auto& piece:surface.pieces) {
                    if(!close_by(piece.lo,piece.hi))continue;
                    std::vector<nav::Vec3> corners;for(const auto p:piece.points)corners.push_back(nav::camera_space(cam,p));
                    std::vector<ImVec2> pixels;for(const auto p:nav::clip_polygon(cam,corners))pixels.push_back(screen(nav::project(cam,p),sx,sy));
                    if(pixels.size()<3)continue;
                    float area=0;for(std::size_t i=0;i<pixels.size();++i){const auto& a=pixels[i];const auto& b=pixels[(i+1)%pixels.size()];area+=a.x*b.y-b.x*a.y;}
                    if(area<0)std::reverse(pixels.begin(),pixels.end());
                    list->AddConvexPolyFilled(pixels.data(),static_cast<int>(pixels.size()),faded(IM_COL32(45,245,95,16),(piece.lo+piece.hi)*0.5f));
                }
                for(const auto& edge:surface.edges) {
                    const nav::Vec3 lo{std::min(edge.a.x,edge.b.x),std::min(edge.a.y,edge.b.y),std::min(edge.a.z,edge.b.z)};
                    const nav::Vec3 hi{std::max(edge.a.x,edge.b.x),std::max(edge.a.y,edge.b.y),std::max(edge.a.z,edge.b.z)};
                    if(!close_by(lo,hi))continue;
                    auto a=nav::camera_space(cam,edge.a),b=nav::camera_space(cam,edge.b);if(!nav::clip_segment(cam,a,b))continue;
                    const auto color=edge.life?IM_COL32(135,255,165,170):IM_COL32(45,235,95,120);
                    list->AddLine(screen(nav::project(cam,a),sx,sy),screen(nav::project(cam,b),sx,sy),faded(color,(edge.a+edge.b)*0.5f),1.0f);
                }
            }
            for(const auto& marker:frame.trigger_snapshot->markers) {
                const auto role=triggers::region(marker.fields);
                const bool area_region=playable_areas&&triggers::playable_region(role);
                const bool general=marker.name!="info_volume"&&trigger_positions;
                if(!area_region&&!general)continue;
                if(area_region&&safe_role(role)&&!general)continue;  // part of the merged surface
                const auto fill=playable_areas&&!area_region?IM_COL32(170,175,185,12):IM_COL32(255,40,40,24);
                const auto outline=playable_areas&&!area_region?IM_COL32(170,175,185,120):IM_COL32(255,65,65,185);
                const auto label_color=playable_areas&&!area_region?IM_COL32(195,200,210,200):IM_COL32(255,150,150,230);
                nav::Vec3 origin{},angles{};if(!triggers::current_pose(marker,origin,angles))continue;
                if(!in_range(marker,origin,angles))continue;bool drawn=false;
                for(const auto& mesh:marker.meshes)for(const auto& face:mesh.faces) {
                    std::vector<nav::Vec3> corners;std::vector<ImVec2> pixels;
                    for(const auto vertex:face)corners.push_back(nav::camera_space(cam,origin+(marker.shape=="Cylinder"?vertex:zombies::rotate(vertex,angles))));
                    for(const auto point:nav::clip_polygon(cam,corners))pixels.push_back(screen(nav::project(cam,point),sx,sy));
                    if(pixels.size()>=3) {
                        float area=0;for(std::size_t i=0;i<pixels.size();++i){const auto& a=pixels[i];const auto& b=pixels[(i+1)%pixels.size()];area+=a.x*b.y-b.x*a.y;}
                        if(area<0)std::reverse(pixels.begin(),pixels.end());
                        list->AddConvexPolyFilled(pixels.data(),static_cast<int>(pixels.size()),fill);
                    }
                    for(std::size_t i=0;i<corners.size();++i) {
                        auto a=corners[i],b=corners[(i+1)%corners.size()];if(!nav::clip_segment(cam,a,b))continue;
                        list->AddLine(screen(nav::project(cam,a),sx,sy),screen(nav::project(cam,b),sx,sy),outline,1.0f);drawn=true;
                    }
                }
                if(drawn)++frame.trigger_count;
                const auto center=nav::camera_space(cam,origin);
                if(trigger_labels&&drawn&&nav::point_visible(cam,center)) {
                    const auto p=screen(nav::project(cam,center),sx,sy);
                    const auto text=triggers::label(marker.name,marker.fields);
                    label(ImVec2(p.x+6,p.y+6),label_color,text.c_str());
                }
            }
        }
        if(collision_part) {
        collision::enable(world_collision);
        // The injection sets the drawn count itself; keep it for the F8 line.
        if(!frame_trace::injected_last_frame())collision_drawn=0;
        collision_batch.clear();collision_gpu_pass=false;
        if(world_collision)if(const auto walls=collision::snapshot()) {
            collision_gpu_failed=!collision_gpu::ready(device);
            // When the injection drew this frame, it already built and drew the collision.
            if(!collision_gpu_failed&&frame_trace::injected_last_frame()) {}
            else if(!collision_gpu_failed) {collision_drawn=build_collision(cam,*walls,sx,sy,collision_batch);collision_gpu_pass=!collision_batch.fills.empty();}
            else collision_drawn=draw_collision(list,cam,*walls,sx,sy);
        }
        // Zombie hitboxes without world collision, when the injection did not run.
        if(zombie_boxes&&!frame_trace::injected_last_frame()&&collision_gpu::ready(device)) {
            collision_batch.viewport_x=cam.x*sx;collision_batch.viewport_y=cam.y*sy;collision_batch.viewport_width=cam.width*sx;collision_batch.viewport_height=cam.height*sy;
            build_zombie_boxes(cam,collision_batch);collision_gpu_pass=!collision_batch.fills.empty();
        }
        }
        if(world&&frame.player_valid)draw_player_box(list,cam,frame.player,sx,sy);
        // Origins-only aids: never drawn on another map, even if left switched on.
        // Drawn at Present, over the weapon and HUD (user request 2026-10-20): the rings and
        // readouts must stay readable over the held staff.
        if(collision_part&&(wind_aid||fire_aid)&&g->map=="maps/mp/zm_tomb.d3dbsp") {
            const auto staffs=staff::snapshot();
            if(wind_aid)draw_wind(list,cam,staffs,width,height,sx,sy,wind_details);
            if(fire_aid)draw_fire(list,cam,staffs,width,height,sx,sy,fire_details);
        }
        if(world&&(mob_skulls||mob_boxes)&&g->map=="maps/mp/zm_prison.d3dbsp")draw_mob(list,cam,mob::snapshot(),height,sx,sy);
        const auto z=zombies::snapshot();
        if(world&&zombie_markers&&z&&GetTickCount64()-z->updated<=200&&z->map->map==g->map&&z->map->base==g->base&&z->map->data==g->data) {
            for(const auto& marker:z->markers) {
                if(!despawn::ui::arrow_visible(frame.despawn_view,marker.number))continue;
                nav::Vec3 origin{};if(!zombies::still_living(marker,origin))continue;
                const auto relative=nav::camera_space(cam,zombies::marker_anchor(origin));
                if(!nav::point_visible(cam,relative))continue;
                const auto p=screen(nav::project(cam,relative),sx,sy);
                // Fixed 12-pixel arrow with one arrow-height clear gap above
                // the projected standing head. No skeleton/fallback switching.
                const ImVec2 left(p.x-6,p.y-24),right(p.x+6,p.y-24),tip(p.x,p.y-12);
                // Draw the black border first so the red interior stays intact.
                const auto marker_color=despawn::ui::arrow_color(frame.despawn_view,marker.number);
                if(marker.special) {
                    const ImVec2 top(p.x,p.y-28),east(p.x+8,p.y-20),bottom(p.x,p.y-12),west(p.x-8,p.y-20);
                    list->AddQuad(top,east,bottom,west,IM_COL32(0,0,0,255),3.0f);
                    list->AddQuadFilled(top,east,bottom,west,marker_color);
                } else {
                    list->AddTriangle(left,right,tip,IM_COL32(0,0,0,255),3.0f);
                    list->AddTriangleFilled(left,right,tip,marker_color);
                }
                if(despawn::ui::arrow_eye(frame.despawn_view,marker.number)) {
                    // Center above either marker, including its black outline,
                    // with clear space above the taller special-enemy diamond.
                    const ImVec2 eye(p.x,p.y-40);
                    list->AddEllipse(eye,ImVec2(8,5),IM_COL32(0,0,0,255),0,24,4);
                    list->AddEllipse(eye,ImVec2(8,5),IM_COL32(255,245,215,255),0,24,2);
                    list->AddCircleFilled(eye,2.5f,IM_COL32(255,245,215,255));
                }
                ++zombie_count;
            }
        }
        list->PopClipRect();
    }
    frame.points=points;frame.edges=edges;frame.zombie_count=zombie_count;
}
void render(IDXGISwapChain* candidate) {
    std::lock_guard lock(guard);if(stopping)return;
    auto saved=ImGui::GetCurrentContext();
    if(!chain&&!attach(candidate)) {ImGui::SetCurrentContext(saved);return;}
    if(candidate!=chain) {ImGui::SetCurrentContext(saved);return;}
    ImGui::SetCurrentContext(gui);
    // The game's frame is complete here: close it for the frame trace before any overlay work.
    {DXGI_SWAP_CHAIN_DESC swap{};if(SUCCEEDED(candidate->GetDesc(&swap)))frame_trace::on_present(swap.BufferDesc.Width,swap.BufferDesc.Height);}
    if(!settings_loaded)load_settings();
    // Unfocused (e.g. tabbed out of borderless fullscreen): keep drawing, but take no
    // input at all, so hotkeys and the menu cannot react to typing in other windows.
    const bool focused=GetForegroundWindow()==window;
    if(!focused) {menu_open=false;close_input();down.fill(false);}
    if(moving_window) {ImGui::SetCurrentContext(saved);return;}
    if(focused) {
    if(pressed(VK_F8))menu_open=!menu_open;
    if(pressed(VK_F9)&&menu_open)menu_open=false;
    if(binding_capture>=0&&menu_open) {
        for(int key=VK_BACK;key<256;++key) {
            if(key==VK_F8||key==VK_F9||key==VK_SHIFT||key==VK_CONTROL||key==VK_MENU||key==VK_LSHIFT||key==VK_RSHIFT||key==VK_LCONTROL||key==VK_RCONTROL||key==VK_LMENU||key==VK_RMENU)continue;
            if(!pressed(key))continue;
            if(key!=VK_ESCAPE) {
                const int assigned=key==VK_BACK?0:key;
                if(assigned)for(auto& binding:quick)if(binding.key==assigned)binding.key=0;
                quick[static_cast<std::size_t>(binding_capture)].key=assigned;
            }
            binding_capture=-1;break;
        }
    } else if(!menu_open) {
        for(std::size_t i=0;i<quick.size();++i) {
            if(quick[i].key&&pressed(quick[i].key))*quick[i].value=!*quick[i].value;
        }
    }
    }
    // Menu and hotkey changes reach the file within a second (and at shutdown).
    if(const auto now=GetTickCount64();now-settings_checked>=1000) {settings_checked=now;save_settings();}
    if(menu_open) {if(!clip_saved)clip_saved=GetClipCursor(&previous_clip)!=FALSE;ClipCursor(nullptr);input_active=true;}
    else close_input();
    if(!visible&&!menu_open) {ImGui::SetCurrentContext(saved);return;}
    ID3D11Texture2D* buffer=nullptr;D3D11_TEXTURE2D_DESC desc{};
    if(FAILED(chain->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&buffer)))) {close_input();ImGui::SetCurrentContext(saved);return;}
    buffer->GetDesc(&desc);if(!target)device->CreateRenderTargetView(buffer,nullptr,&target);
    // Next frame, the first game draw onto this backbuffer triggers inject_collision.
    backbuffer_width=desc.Width;backbuffer_height=desc.Height;
    frame_trace::set_injection(visible&&!collision_gpu_failed?&inject_collision:nullptr,buffer);
    release(buffer);
    if(target&&desc.Width&&desc.Height) {
        ImGui::SetCurrentContext(gui);
        ImGui_ImplDX11_NewFrame();ImGui_ImplWin32_NewFrame();auto& io=ImGui::GetIO();
        const auto now=GetTickCount64();io.DeltaTime=last_frame?std::max(0.001f,static_cast<float>(now-last_frame)/1000):1.0f/60;last_frame=now;
        io.DisplaySize=ImVec2(static_cast<float>(desc.Width),static_cast<float>(desc.Height));
        if(menu_open) {
            POINT cursor{};if(GetCursorPos(&cursor)&&ScreenToClient(window,&cursor))io.AddMousePosEvent(static_cast<float>(cursor.x),static_cast<float>(cursor.y));
            const int buttons[]={VK_LBUTTON,VK_RBUTTON,VK_MBUTTON,VK_XBUTTON1,VK_XBUTTON2};
            for(int i=0;i<5;++i)io.AddMouseButtonEvent(i,(GetAsyncKeyState(buttons[i])&0x8000)!=0);
            if(wheel!=0) {io.AddMouseWheelEvent(0,wheel);wheel=0;}
        }
        io.MouseDrawCursor=menu_open;
        ImGui::NewFrame();
        FrameInfo frame{};frame.graph=nav::graph();
        frame.camera_valid=frame.graph&&nav::read_camera(frame.camera,*frame.graph);
        frame.player_valid=player_bounds&&frame.camera_valid&&triggers::player_box(frame.player);
        frame.despawn_view=despawn::ui::capture(frame.graph);
        last_despawn_view=frame.despawn_view;
        frame.spawn_snapshot=spawns::snapshot();
        frame.trigger_snapshot=triggers::snapshot();
        if(frame.trigger_snapshot&&(!frame.graph||!triggers::fresh(*frame.trigger_snapshot,*frame.graph,now)))frame.trigger_snapshot.reset();
        const auto& s=frame.spawn_snapshot;
        frame.spawn_valid=s&&frame.graph&&now-s->updated<=2000&&s->map->map==frame.graph->map&&s->map->base==frame.graph->base&&s->map->data==frame.graph->data;
        if(visible) {
            auto* background=ImGui::GetBackgroundDrawList();const int first=background->VtxBuffer.Size;
            frame_paused=game_paused(now);
            const bool world_done=frame_trace::injected_last_frame()&&world_injected;
            draw(background,io.DisplaySize.x,io.DisplaySize.y,frame,!world_done,true);
            if(world_done) {frame.spawn_count=world_counts.spawns;frame.trigger_count=world_counts.triggers;frame.player_overlaps=world_counts.overlaps;}
            draw_readouts(io.DisplaySize.x,io.DisplaySize.y);despawn::ui::hud(frame.despawn_view,io.DisplaySize.x,io.DisplaySize.y);
            // Behind the semi-transparent pause menu: fade everything drawn this frame
            // (the GPU collision batch is faded while it is built).
            if(frame_paused)fade(*background,first,paused_strength);
        }
        draw_hint(now,io.DisplaySize.y);
        if(menu_open)menu(frame);
        if(!menu_open)close_input();ImGui::Render();
        ID3D11RenderTargetView* old[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};ID3D11DepthStencilView* depth=nullptr;
        context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,old,&depth);
        frame_trace::overlay_begin();
        if(collision_gpu_pass&&visible)collision_gpu::draw(device,context,target,desc.Width,desc.Height,collision_batch);
        context->OMSetRenderTargets(1,&target,nullptr);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,old,depth);
        frame_trace::overlay_end();
        for(auto& t:old)release(t);release(depth);
    } else close_input();
    ImGui::SetCurrentContext(saved);
}
HRESULT __stdcall present(IDXGISwapChain* candidate,UINT interval,UINT flags) {
    if(!(flags&DXGI_PRESENT_TEST)) {try {render(candidate);}catch(...) {nav::clear_graph();}}
    return next_present(candidate,interval,flags);
}
HRESULT __stdcall resize(IDXGISwapChain* candidate,UINT count,UINT width,UINT height,DXGI_FORMAT format,UINT flags) {
    std::lock_guard lock(guard);if(candidate==chain)release(target);
    return next_resize(candidate,count,width,height,format,flags);
}
}
bool startup() {
    // Retain this DLL until process exit: no unloading while graphics or SDK
    // callbacks may still be in flight. Normal restart is the supported removal.
    HMODULE self=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&startup),&self))return false;
    const auto module=GetModuleHandleW(nullptr);
    WNDCLASSW wc{};wc.hInstance=module;wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"T6Gnosis_DXGI_Probe";
    if(!RegisterClassW(&wc))return false;
    HWND probe=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,2,2,nullptr,nullptr,module,nullptr);
    if(!probe) {UnregisterClassW(wc.lpszClassName,module);return false;}
    DXGI_SWAP_CHAIN_DESC desc{};desc.BufferCount=1;desc.BufferDesc.Width=2;desc.BufferDesc.Height=2;
    desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow=probe;desc.SampleDesc.Count=1;desc.Windowed=TRUE;
    IDXGISwapChain* test=nullptr;ID3D11Device* test_device=nullptr;
    bool ok=false;
    if(SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&test,&test_device,nullptr,nullptr))) {
        auto** table=*reinterpret_cast<void***>(test);present_address=table[8];resize_address=table[13];
        if(MH_Initialize()==MH_OK) {
            ok=MH_CreateHook(present_address,reinterpret_cast<void*>(present),reinterpret_cast<void**>(&next_present))==MH_OK&&
                MH_CreateHook(resize_address,reinterpret_cast<void*>(resize),reinterpret_cast<void**>(&next_resize))==MH_OK;
            if(ok)ok=MH_QueueEnableHook(present_address)==MH_OK&&MH_QueueEnableHook(resize_address)==MH_OK&&MH_ApplyQueued()==MH_OK;
            if(!ok) {MH_DisableHook(present_address);MH_DisableHook(resize_address);MH_Uninitialize();}
        }
    }
    release(test);release(test_device);DestroyWindow(probe);UnregisterClassW(wc.lpszClassName,module);return ok;
}
void shutdown() {
    stopping=true;if(present_address)MH_DisableHook(present_address);if(resize_address)MH_DisableHook(resize_address);
    std::lock_guard lock(guard);auto saved=ImGui::GetCurrentContext();const bool ours=saved==gui;
    save_settings();cleanup();ImGui::SetCurrentContext(ours?nullptr:saved);
}
}
