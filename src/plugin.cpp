#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <plutonium_sdk.hpp>
#include "navigation.hpp"
#include "overlay.hpp"
#include "zombies.hpp"
#include "triggers.hpp"
#include "spawns.hpp"
#include "practice.hpp"
#include "despawn_bridge.hpp"
#include "despawn.hpp"
#include "despawn_schedule.hpp"
#include "staff_aid.hpp"
#include "mob_aid.hpp"
#include "world_collision.hpp"
#include "helper_deploy.hpp"
#include "version.hpp"

namespace {
// The helper's "plugin present" test (builtinfunctionexists); it never runs anything.
void PLUTONIUM_CALLBACK plugin_loaded() {}
void clear_session(int unused) {nav::clear_graph(unused);zombies::clear();spawns::clear();despawn::clear();triggers::clear();staff::clear();mob::clear();collision::clear();}
void update_zombies() {try {zombies::update();despawn::schedule::update();}catch(...) {zombies::clear();despawn::schedule::clear();}}
plutonium::sdk::interfaces::scheduler::evaluation update_spawns() {
    try {spawns::update();}catch(...) {spawns::clear();} try {triggers::update();}catch(...) {triggers::clear();} try {collision::update();}catch(...) {collision::clear();}
    return plutonium::sdk::interfaces::scheduler::reschedule;
}
plutonium::sdk::interfaces::scheduler::evaluation update() {
    try {nav::refresh_graph();} catch(...) {nav::clear_graph();}
    return plutonium::sdk::interfaces::scheduler::reschedule;
}
class Navigation final:public plutonium::sdk::plugin {
    bool active=false;
    const char* plugin_name() override {return "T6 Gnosis (solo LAN)";}
    bool is_game_supported(plutonium::sdk::game game) override {return game==plutonium::sdk::game::t6;}
    void on_startup(plutonium::sdk::iinterface* api,plutonium::sdk::game) override {
        // Console output: one ready line, plus a line only for a part that cannot start
        // (normally an unsupported game build).
        auto unavailable=[&](bool ok,const char* text) {if(!ok)api->logging()->info(text);};
        // Registered first and unconditionally: the helper does nothing without it.
        api->gsc()->register_function("GnosisPluginLoaded",plugin_loaded);
        unavailable(helper_deploy::deploy()!=helper_deploy::Result::failed,"[T6 Gnosis] Could not write the GSC helper; helper features stay off.");
        unavailable(spawns::startup(api),"[T6 Gnosis] Spawn reader unavailable on this game build.");
        // A script restart can reuse the same graph allocation. Reset publisher
        // sequence and sites at script execution, as well as game shutdown.
        api->callbacks()->on_scripts_execute(spawns::clear);
        api->callbacks()->on_scripts_execute(triggers::clear);
        api->callbacks()->on_scripts_execute(despawn::clear);
        api->callbacks()->on_scripts_execute(staff::clear);
        api->callbacks()->on_scripts_execute(mob::clear);
        api->callbacks()->on_scripts_execute(collision::clear);
        unavailable(staff::startup(api),"[T6 Gnosis] Origins aids unavailable on this game build.");
        unavailable(mob::startup(api),"[T6 Gnosis] Mob of the Dead aids unavailable on this game build.");
        unavailable(despawn::startup(api),"[T6 Gnosis] Despawn tracker unavailable on this game build.");
        if(!nav::bindings_match()) {api->logging()->info("[T6 Gnosis] Unsupported game build; disabled.");return;}
        active=overlay::startup();
        if(!active) {api->logging()->info("[T6 Gnosis] Could not hook the renderer; disabled.");return;}
        practice::startup(api);
        unavailable(zombies::startup(),"[T6 Gnosis] Zombie markers unavailable on this game build.");
        unavailable(triggers::startup(),"[T6 Gnosis] Trigger geometry unavailable on this game build.");
        api->callbacks()->on_game_shutdown(clear_session);
        api->scheduler()->every(update,1000,plutonium::sdk::interfaces::scheduler::thread::game);
        api->scheduler()->every(update_spawns,500,plutonium::sdk::interfaces::scheduler::thread::game);
        api->scheduler()->on_frame(update_zombies,plutonium::sdk::interfaces::scheduler::thread::game);
        api->logging()->info("[T6 Gnosis] " GNOSIS_VERSION " ready. F8 opens the menu.");
    }
    void on_shutdown() override {practice::shutdown();if(active)overlay::shutdown();nav::clear_graph();zombies::clear();spawns::clear();despawn::clear();triggers::clear();staff::clear();mob::clear();collision::clear();}
};
}
PLUTONIUM_API plutonium::sdk::plugin* on_initialize() {static Navigation plugin;return &plugin;}
BOOL APIENTRY DllMain(HMODULE,DWORD,LPVOID) {return TRUE;}
