#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "frame_trace.hpp"
#include <MinHook.h>
#include <mutex>
#include <vector>

namespace frame_trace {
namespace {
using SetTargets=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,ID3D11RenderTargetView* const*,ID3D11DepthStencilView*);
using SetTargetsUav=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,ID3D11RenderTargetView* const*,ID3D11DepthStencilView*,UINT,UINT,ID3D11UnorderedAccessView* const*,const UINT*);
using DrawIndexedFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,INT);
using DrawFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT);
using DrawIndexedInstancedFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT,INT,UINT);
using DrawInstancedFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT,UINT,UINT);
SetTargets next_set_targets=nullptr;SetTargetsUav next_set_targets_uav=nullptr;
DrawIndexedFn next_draw_indexed=nullptr;DrawFn next_draw=nullptr;DrawIndexedInstancedFn next_draw_indexed_instanced=nullptr;DrawInstancedFn next_draw_instanced=nullptr;
std::vector<void*> hooked;

std::mutex guard;
ID3D11DeviceContext* immediate=nullptr;
bool overlay=false;
// Depth view bound now (the pipeline holds it alive while bound) and the views seen
// this frame, each AddRef'd until the frame ends.
ID3D11DepthStencilView* bound=nullptr;
struct Seen {ID3D11DepthStencilView* view=nullptr;unsigned draws=0;};
std::vector<Seen> seen;int bound_index=-1;
ID3D11DepthStencilView* candidate=nullptr;
Inject inject=nullptr;ID3D11Resource* backbuffer=nullptr;  // compared by identity only
ID3D11Resource* bound_target=nullptr;bool injected=false,last_injected=false;

template<class T> void drop(T*& p) {if(p){p->Release();p=nullptr;}}
bool size_of(ID3D11DepthStencilView* view,unsigned& width,unsigned& height) {
    ID3D11Resource* resource=nullptr;view->GetResource(&resource);if(!resource)return false;
    ID3D11Texture2D* texture=nullptr;bool ok=SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&texture)));
    if(ok) {D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);width=d.Width;height=d.Height;}
    drop(texture);drop(resource);return ok;
}
void track(ID3D11DepthStencilView* view) {
    bound=view;bound_index=-1;if(!view)return;
    for(std::size_t i=0;i<seen.size();++i)if(seen[i].view==view){bound_index=static_cast<int>(i);return;}
    view->AddRef();seen.push_back({view,0});bound_index=static_cast<int>(seen.size()-1);
}
void targets(ID3D11DeviceContext* c,UINT count,ID3D11RenderTargetView* const* views,ID3D11DepthStencilView* depth,bool keep) {
    if(c!=immediate||overlay||keep)return;
    std::lock_guard lock(guard);
    track(depth);
    bound_target=nullptr;
    if(count&&views&&views[0]) {ID3D11Resource* r=nullptr;views[0]->GetResource(&r);bound_target=r;if(r)r->Release();}
}
void draw_one(ID3D11DeviceContext* c) {
    if(c!=immediate||overlay)return;
    std::lock_guard lock(guard);
    if(bound&&bound_index<0)track(bound);  // still bound from an earlier frame
    if(bound_index>=0)++seen[static_cast<std::size_t>(bound_index)].draws;
}
void STDMETHODCALLTYPE set_targets(ID3D11DeviceContext* c,UINT n,ID3D11RenderTargetView* const* v,ID3D11DepthStencilView* d) {targets(c,n,v,d,false);next_set_targets(c,n,v,d);}
void STDMETHODCALLTYPE set_targets_uav(ID3D11DeviceContext* c,UINT n,ID3D11RenderTargetView* const* v,ID3D11DepthStencilView* d,UINT a,UINT b,ID3D11UnorderedAccessView* const* u,const UINT* i) {
    targets(c,n,v,d,n==D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL);next_set_targets_uav(c,n,v,d,a,b,u,i);
}
// After a game draw: the first one onto the backbuffer each frame triggers the injection.
// The lock is released before the callback, whose own calls re-enter these hooks.
void after_draw(ID3D11DeviceContext* c) {
    Inject callback=nullptr;
    {
        std::lock_guard lock(guard);
        if(c!=immediate||overlay||!inject||injected||!backbuffer||bound_target!=backbuffer)return;
        injected=true;overlay=true;callback=inject;
    }
    callback();
    std::lock_guard lock(guard);overlay=false;
}
void STDMETHODCALLTYPE draw_indexed(ID3D11DeviceContext* c,UINT a,UINT b,INT d) {draw_one(c);next_draw_indexed(c,a,b,d);after_draw(c);}
void STDMETHODCALLTYPE draw(ID3D11DeviceContext* c,UINT a,UINT b) {draw_one(c);next_draw(c,a,b);after_draw(c);}
void STDMETHODCALLTYPE draw_indexed_instanced(ID3D11DeviceContext* c,UINT a,UINT b,UINT d,INT e,UINT f) {draw_one(c);next_draw_indexed_instanced(c,a,b,d,e,f);after_draw(c);}
void STDMETHODCALLTYPE draw_instanced(ID3D11DeviceContext* c,UINT a,UINT b,UINT d,UINT e) {draw_one(c);next_draw_instanced(c,a,b,d,e);after_draw(c);}
bool hook(void* target,void* detour,void** original) {
    if(MH_CreateHook(target,detour,original)!=MH_OK)return false;
    if(MH_EnableHook(target)!=MH_OK) {MH_RemoveHook(target);return false;}
    hooked.push_back(target);return true;
}
}
bool install(ID3D11DeviceContext* context) {
    if(!context||immediate)return immediate==context;
    auto** table=*reinterpret_cast<void***>(context);
    // ID3D11DeviceContext vtable slots: DrawIndexed 12, Draw 13, DrawIndexedInstanced 20,
    // DrawInstanced 21, OMSetRenderTargets 33, ...AndUnorderedAccessViews 34.
    const bool ok=hook(table[33],reinterpret_cast<void*>(&set_targets),reinterpret_cast<void**>(&next_set_targets))&&
        hook(table[34],reinterpret_cast<void*>(&set_targets_uav),reinterpret_cast<void**>(&next_set_targets_uav))&&
        hook(table[12],reinterpret_cast<void*>(&draw_indexed),reinterpret_cast<void**>(&next_draw_indexed))&&
        hook(table[13],reinterpret_cast<void*>(&draw),reinterpret_cast<void**>(&next_draw))&&
        hook(table[20],reinterpret_cast<void*>(&draw_indexed_instanced),reinterpret_cast<void**>(&next_draw_indexed_instanced))&&
        hook(table[21],reinterpret_cast<void*>(&draw_instanced),reinterpret_cast<void**>(&next_draw_instanced));
    if(!ok) {uninstall();return false;}
    immediate=context;return true;
}
void uninstall() {
    for(auto* target:hooked) {MH_DisableHook(target);MH_RemoveHook(target);}
    hooked.clear();
    std::lock_guard lock(guard);
    for(auto& s:seen)drop(s.view);
    seen.clear();bound=nullptr;bound_index=-1;drop(candidate);immediate=nullptr;inject=nullptr;backbuffer=nullptr;bound_target=nullptr;
}
void overlay_begin() {std::lock_guard lock(guard);overlay=true;}
void overlay_end() {std::lock_guard lock(guard);overlay=false;}
void set_injection(Inject callback,ID3D11Resource* target) {std::lock_guard lock(guard);inject=callback;backbuffer=target;}
bool injected_last_frame() {std::lock_guard lock(guard);return last_injected;}
void on_present(unsigned width,unsigned height) {
    std::lock_guard lock(guard);
    last_injected=injected;injected=false;
    // Scene depth candidate: the backbuffer-sized depth view with the most draws.
    ID3D11DepthStencilView* best=nullptr;unsigned best_draws=0;
    for(const auto& s:seen) {
        unsigned w=0,h=0;
        if(s.draws>best_draws&&size_of(s.view,w,h)&&w==width&&h==height) {best=s.view;best_draws=s.draws;}
    }
    if(best!=candidate) {drop(candidate);if(best){best->AddRef();candidate=best;}}
    for(auto& s:seen)drop(s.view);
    seen.clear();bound_index=-1;
}
ID3D11DepthStencilView* scene_depth() {std::lock_guard lock(guard);if(candidate)candidate->AddRef();return candidate;}
}
