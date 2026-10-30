#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "frame_trace.hpp"
#include "collision_gpu.hpp"
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
bool mask_on=false,mask_done=false;

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
// After a game draw: an opaque model colour draw (on the scene depth, with a colour target
// and depth writes, 32-byte vertices) is drawn again into the model mask. 36-byte draws are
// map geometry; their second stream (4-16 bytes) is not a prop marker: masking them hid
// terrain and walls (user screenshots 2026-10-30). The model draw is redrawn with only
// the pixel shader, target and depth state swapped. Everything changed is restored before
// the game continues.
template<class Redraw> void mask_draw(ID3D11DeviceContext* c,Redraw redraw) {
    unsigned width=0,height=0;bool clear=false;
    {
        std::lock_guard lock(guard);
        if(c!=immediate||overlay||!mask_on||!candidate||bound!=candidate||!bound_target)return;
        ID3D11Buffer* buffers[2]{};UINT strides[2]{},offsets[2]{};c->IAGetVertexBuffers(0,2,buffers,strides,offsets);
        const bool model=buffers[0]&&strides[0]==32;
        for(auto* b:buffers)if(b)b->Release();
        if(!model)return;
        ID3D11DepthStencilState* state=nullptr;UINT reference=0;c->OMGetDepthStencilState(&state,&reference);
        D3D11_DEPTH_STENCIL_DESC desc{};if(state){state->GetDesc(&desc);state->Release();}
        if(!desc.DepthEnable||desc.DepthWriteMask!=D3D11_DEPTH_WRITE_MASK_ALL)return;
        ID3D11Texture2D* texture=nullptr;
        if(FAILED(bound_target->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&texture))))return;
        D3D11_TEXTURE2D_DESC t{};texture->GetDesc(&t);texture->Release();width=t.Width;height=t.Height;
        clear=!mask_done;overlay=true;
    }
    ID3D11Device* device=nullptr;c->GetDevice(&device);
    collision_gpu::MaskPass pass;
    if(device&&collision_gpu::mask_begin(device,c,width,height,clear,pass)) {
        ID3D11RenderTargetView* targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};ID3D11DepthStencilView* depth=nullptr;
        c->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,targets,&depth);
        ID3D11PixelShader* shader=nullptr;ID3D11ClassInstance* instances[256]{};UINT instance_count=256;
        c->PSGetShader(&shader,instances,&instance_count);
        ID3D11DepthStencilState* state=nullptr;UINT reference=0;c->OMGetDepthStencilState(&state,&reference);
        ID3D11BlendState* blend=nullptr;float factor[4]{};UINT sample_mask=0;c->OMGetBlendState(&blend,factor,&sample_mask);
        c->OMSetRenderTargets(1,&pass.target,depth);c->PSSetShader(pass.shader,nullptr,0);
        c->OMSetDepthStencilState(pass.depth,0);c->OMSetBlendState(nullptr,nullptr,0xFFFFFFFF);
        redraw();
        c->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,targets,depth);
        c->PSSetShader(shader,instances,instance_count);
        c->OMSetDepthStencilState(state,reference);c->OMSetBlendState(blend,factor,sample_mask);
        for(auto* t:targets)drop(t);drop(depth);drop(shader);for(UINT i=0;i<instance_count&&i<256;++i)drop(instances[i]);drop(state);drop(blend);
        std::lock_guard lock(guard);mask_done=true;
    }
    if(device)device->Release();
    std::lock_guard lock(guard);overlay=false;
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
void STDMETHODCALLTYPE draw_indexed(ID3D11DeviceContext* c,UINT a,UINT b,INT d) {draw_one(c);next_draw_indexed(c,a,b,d);mask_draw(c,[&]{next_draw_indexed(c,a,b,d);});after_draw(c);}
void STDMETHODCALLTYPE draw(ID3D11DeviceContext* c,UINT a,UINT b) {draw_one(c);next_draw(c,a,b);mask_draw(c,[&]{next_draw(c,a,b);});after_draw(c);}
void STDMETHODCALLTYPE draw_indexed_instanced(ID3D11DeviceContext* c,UINT a,UINT b,UINT d,INT e,UINT f) {draw_one(c);next_draw_indexed_instanced(c,a,b,d,e,f);mask_draw(c,[&]{next_draw_indexed_instanced(c,a,b,d,e,f);});after_draw(c);}
void STDMETHODCALLTYPE draw_instanced(ID3D11DeviceContext* c,UINT a,UINT b,UINT d,UINT e) {draw_one(c);next_draw_instanced(c,a,b,d,e);mask_draw(c,[&]{next_draw_instanced(c,a,b,d,e);});after_draw(c);}
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
    seen.clear();bound=nullptr;bound_index=-1;drop(candidate);immediate=nullptr;inject=nullptr;backbuffer=nullptr;bound_target=nullptr;mask_on=false;
}
void overlay_begin() {std::lock_guard lock(guard);overlay=true;}
void overlay_end() {std::lock_guard lock(guard);overlay=false;}
void set_injection(Inject callback,ID3D11Resource* target) {std::lock_guard lock(guard);inject=callback;backbuffer=target;}
bool injected_last_frame() {std::lock_guard lock(guard);return last_injected;}
void set_model_mask(bool on) {std::lock_guard lock(guard);mask_on=on;}
bool mask_written() {std::lock_guard lock(guard);return mask_done;}
void on_present(unsigned width,unsigned height) {
    std::lock_guard lock(guard);
    last_injected=injected;injected=false;mask_done=false;
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
