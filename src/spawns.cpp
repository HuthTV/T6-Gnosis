#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <plutonium_sdk.hpp>
#include <atomic>
#include <mutex>
#include "spawns_batch.hpp"
#include "spawns_native.hpp"

namespace spawns {
namespace {
std::mutex guard;
std::atomic_bool ready=false;
std::uint64_t generation=0;
std::shared_ptr<const Snapshot> current;
const char* state="Waiting for native spawn registry";
}
bool startup(plutonium::sdk::v1::iinterface* api) {
    ready=false;if(!api)return false;
    const bool supported=native::startup();
    {std::lock_guard lock(guard);++generation;current.reset();state=supported?"Waiting for native spawn registry":"Native spawn layout mismatch; markers disabled";}
    ready=supported;return supported;
}
void update() {
    if(!ready.load())return;
    std::uint64_t capture_generation=0;
    {std::lock_guard lock(guard);capture_generation=generation;}
    auto g=nav::graph();
    native::Capture captured;
    try {captured=native::collect(g);}
    catch(...) {native::clear();captured.reason="Native spawn capture failed";}
    std::lock_guard lock(guard);
    if(capture_generation!=generation)return;
    state=captured.reason;
    if(captured.state==native::State::ready&&captured.data) {current=std::move(captured.data);return;}
    // Keep the last good snapshot through a failed capture on the same map: the registry
    // changing mid-read (spawning zombies) used to blank the sites for a moment, a
    // periodic flicker (user report 2026-10-13). A new map or 2 s of failures still clears.
    if(current&&g&&current->map&&same_map(*g,*current->map)&&GetTickCount64()-current->updated<=2000)return;
    current.reset();
}
void clear() {
    // Native cache is always cleared before the publication mutex is acquired.
    // update releases the cache mutex before publication; no reverse lock exists.
    native::clear();
    std::lock_guard lock(guard);++generation;current.reset();state=ready.load()?"Waiting for native spawn registry / restart":"Native spawn bindings disabled";
}
std::shared_ptr<const Snapshot> snapshot() {std::lock_guard lock(guard);return current;}
const char* status() {std::lock_guard lock(guard);return state;}
}
