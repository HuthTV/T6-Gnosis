#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "collision_gpu.hpp"
#include <d3dcompiler.h>
#include <algorithm>
#include <cstddef>
#include <cstring>

namespace collision_gpu {
namespace {
template<class T> void drop(T*& p) {if(p){p->Release();p=nullptr;}}
ID3D11Device* owner=nullptr;
bool failed=false;
ID3D11VertexShader* vertex_shader=nullptr;ID3D11PixelShader* pixel_shader=nullptr;ID3D11InputLayout* layout=nullptr;
ID3D11Buffer* vertices=nullptr;UINT vertex_capacity=0;ID3D11Buffer* constants=nullptr;
ID3D11Texture2D* depth_texture=nullptr;ID3D11DepthStencilView* depth_view=nullptr;unsigned depth_width=0,depth_height=0;
ID3D11DepthStencilState* depth_write=nullptr;ID3D11DepthStencilState* depth_test=nullptr;ID3D11DepthStencilState* depth_off=nullptr;
ID3D11BlendState* no_color=nullptr;ID3D11BlendState* alpha_blend=nullptr;
ID3D11RasterizerState* raster=nullptr;
// Readable view of the game's scene depth: on the texture itself when it allows
// shader reads, otherwise on a private copy refreshed every frame.
ID3D11Texture2D* scene_source=nullptr;ID3D11Texture2D* scene_copy=nullptr;ID3D11ShaderResourceView* scene_view=nullptr;D3D11_TEXTURE2D_DESC scene_desc{};

// Vertex positions arrive in clip space; w is the view distance, passed on for the
// scene-depth test. Scene depth is reversed and infinite: distance = near / depth.
// A nearer scene point covers collision only when it is the weapon/hands (closer than
// lens.w) or lies inside a zombie box; it is rebuilt from the pixel, depth and camera
// using the overlay's projection (ndc x = -left/(z tan_x), ndc y = up/(z tan_y)).
constexpr char source[]=
    "cbuffer C:register(b0){float4 origin;float4 forward;float4 left;float4 up;"
    " float4 lens;float4 port;float4 test;float4 scale;float4 boxes[64];};"
    "Texture2D<float> scene:register(t0);"
    "struct VI{float4 pos:POSITION;float4 col:COLOR0;};"
    "struct PI{float4 pos:SV_POSITION;float4 col:COLOR0;float view:TEXCOORD0;};"
    "PI vs(VI i){PI o;o.pos=i.pos;o.col=i.col;o.view=i.pos.w;return o;}"
    "float4 ps(PI i):SV_Target{"
    " if(test.z>0.5){float d=scene.Load(int3(int2(i.pos.xy*scale.xy),0));"
    "  if(d>1e-7){float z=lens.z/d;"
    "   if(z<i.view-max(test.x,test.y*i.view)){"
    "    bool hide=z<lens.w;"
    "    if(!hide){float nx=(i.pos.x-port.x)/port.z*2-1;float ny=1-(i.pos.y-port.y)/port.w*2;"
    "     float3 p=origin.xyz+forward.xyz*z-left.xyz*(nx*z*lens.x)+up.xyz*(ny*z*lens.y);"
    "     int n=(int)test.w;"
    "     for(int b=0;b<32;++b){if(b>=n)break;if(all(p>=boxes[b*2].xyz)&&all(p<=boxes[b*2+1].xyz)){hide=true;break;}}}"
    "    if(hide)discard;}}}"
    " return i.col;}";
// ImGui draw lists (world overlays) in the same pass (user request 2026-10-14: under
// the weapon and HUD by default). Positions are target pixels; the font atlas is t1.
// They carry no depth, so only the weapon and zombies cover them: a fragment is
// dropped where the scene pixel is the viewmodel or lies inside a zombie box.
constexpr char list_source[]=
    "cbuffer C:register(b0){float4 origin;float4 forward;float4 left;float4 up;"
    " float4 lens;float4 port;float4 test;float4 scale;float4 boxes[64];};"
    "Texture2D<float> scene:register(t0);Texture2D font:register(t1);SamplerState linear_sampler:register(s0);"
    "struct VI{float2 pos:POSITION;float2 uv:TEXCOORD0;float4 col:COLOR0;};"
    "struct PI{float4 pos:SV_POSITION;float2 uv:TEXCOORD0;float4 col:COLOR0;};"
    "PI vs(VI i){PI o;o.pos=float4(i.pos.x/scale.z*2-1,1-i.pos.y/scale.w*2,0,1);o.uv=i.uv;o.col=i.col;return o;}"
    "float4 ps(PI i):SV_Target{"
    " if(test.z>0.5){float d=scene.Load(int3(int2(i.pos.xy*scale.xy),0));"
    "  if(d>1e-7){float z=lens.z/d;bool hide=z<lens.w;"
    "   if(!hide){float nx=(i.pos.x-port.x)/port.z*2-1;float ny=1-(i.pos.y-port.y)/port.w*2;"
    "    float3 p=origin.xyz+forward.xyz*z-left.xyz*(nx*z*lens.x)+up.xyz*(ny*z*lens.y);"
    "    int n=(int)test.w;"
    "    for(int b=0;b<32;++b){if(b>=n)break;if(all(p>=boxes[b*2].xyz)&&all(p<=boxes[b*2+1].xyz)){hide=true;break;}}}"
    "   if(hide)discard;}}"
    " return i.col*font.Sample(linear_sampler,i.uv);}";
ID3D11VertexShader* list_vertex_shader=nullptr;ID3D11PixelShader* list_pixel_shader=nullptr;ID3D11InputLayout* list_layout=nullptr;
ID3D11Buffer* list_vertices=nullptr;UINT list_vertex_capacity=0;ID3D11Buffer* list_indices=nullptr;UINT list_index_capacity=0;
ID3D11SamplerState* list_sampler=nullptr;ID3D11RasterizerState* list_raster=nullptr;
struct Constants {
    float origin[4]{},forward[4]{},left[4]{},up[4]{};
    float lens[4]{1,1,4,48};    // tan_x, tan_y, near, weapon distance
    float port[4]{0,0,1,1};     // viewport x, y, width, height in target pixels
    float test[4]{8,0.03f,0,0}; // tolerance, relative, enabled, box count
    float scale[4]{1,1,0,0};    // scene texels per target pixel
    float boxes[64][4]{};       // lo, hi pairs
};
static_assert(sizeof(Constants)==72*16);

bool create(ID3D11Device* device) {
    ID3DBlob* vs=nullptr;ID3DBlob* ps=nullptr;
    bool ok=SUCCEEDED(D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"vs","vs_4_0",0,0,&vs,nullptr))&&
        SUCCEEDED(D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"ps","ps_4_0",0,0,&ps,nullptr))&&
        SUCCEEDED(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex_shader))&&
        SUCCEEDED(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel_shader));
    if(ok) {
        const D3D11_INPUT_ELEMENT_DESC elements[]={
            {"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,16,D3D11_INPUT_PER_VERTEX_DATA,0}};
        ok=SUCCEEDED(device->CreateInputLayout(elements,2,vs->GetBufferPointer(),vs->GetBufferSize(),&layout));
    }
    drop(vs);drop(ps);
    if(ok) {
        D3D11_BUFFER_DESC b{};b.ByteWidth=sizeof(Constants);b.Usage=D3D11_USAGE_DYNAMIC;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;b.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        ok=SUCCEEDED(device->CreateBuffer(&b,nullptr,&constants));
    }
    if(ok) {
        D3D11_DEPTH_STENCIL_DESC d{};d.DepthEnable=TRUE;d.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;d.DepthFunc=D3D11_COMPARISON_LESS;
        ok=SUCCEEDED(device->CreateDepthStencilState(&d,&depth_write));
        d.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;d.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
        ok=ok&&SUCCEEDED(device->CreateDepthStencilState(&d,&depth_test));
        d.DepthEnable=FALSE;
        ok=ok&&SUCCEEDED(device->CreateDepthStencilState(&d,&depth_off));
    }
    if(ok) {
        D3D11_BLEND_DESC b{};b.RenderTarget[0].RenderTargetWriteMask=0;
        ok=SUCCEEDED(device->CreateBlendState(&b,&no_color));
        auto& t=b.RenderTarget[0];t.BlendEnable=TRUE;t.SrcBlend=D3D11_BLEND_SRC_ALPHA;t.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;t.BlendOp=D3D11_BLEND_OP_ADD;
        t.SrcBlendAlpha=D3D11_BLEND_ONE;t.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;t.BlendOpAlpha=D3D11_BLEND_OP_ADD;t.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        ok=ok&&SUCCEEDED(device->CreateBlendState(&b,&alpha_blend));
    }
    if(ok) {
        D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;
        ok=SUCCEEDED(device->CreateRasterizerState(&r,&raster));
        r.ScissorEnable=TRUE;
        ok=ok&&SUCCEEDED(device->CreateRasterizerState(&r,&list_raster));
    }
    if(ok) {
        ID3DBlob* list_vs=nullptr;ID3DBlob* list_ps=nullptr;
        ok=SUCCEEDED(D3DCompile(list_source,sizeof(list_source)-1,nullptr,nullptr,nullptr,"vs","vs_4_0",0,0,&list_vs,nullptr))&&
            SUCCEEDED(D3DCompile(list_source,sizeof(list_source)-1,nullptr,nullptr,nullptr,"ps","ps_4_0",0,0,&list_ps,nullptr))&&
            SUCCEEDED(device->CreateVertexShader(list_vs->GetBufferPointer(),list_vs->GetBufferSize(),nullptr,&list_vertex_shader))&&
            SUCCEEDED(device->CreatePixelShader(list_ps->GetBufferPointer(),list_ps->GetBufferSize(),nullptr,&list_pixel_shader));
        if(ok) {
            const D3D11_INPUT_ELEMENT_DESC elements[]={
                {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,static_cast<UINT>(offsetof(ImDrawVert,pos)),D3D11_INPUT_PER_VERTEX_DATA,0},
                {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,static_cast<UINT>(offsetof(ImDrawVert,uv)),D3D11_INPUT_PER_VERTEX_DATA,0},
                {"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,static_cast<UINT>(offsetof(ImDrawVert,col)),D3D11_INPUT_PER_VERTEX_DATA,0}};
            ok=SUCCEEDED(device->CreateInputLayout(elements,3,list_vs->GetBufferPointer(),list_vs->GetBufferSize(),&list_layout));
        }
        drop(list_vs);drop(list_ps);
    }
    if(ok) {
        D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;s.ComparisonFunc=D3D11_COMPARISON_ALWAYS;
        ok=SUCCEEDED(device->CreateSamplerState(&s,&list_sampler));
    }
    return ok;
}
bool ensure_depth(ID3D11Device* device,unsigned width,unsigned height) {
    if(depth_view&&depth_width==width&&depth_height==height)return true;
    drop(depth_view);drop(depth_texture);depth_width=depth_height=0;
    D3D11_TEXTURE2D_DESC t{};t.Width=width;t.Height=height;t.MipLevels=1;t.ArraySize=1;t.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;
    t.SampleDesc.Count=1;t.Usage=D3D11_USAGE_DEFAULT;t.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    if(FAILED(device->CreateTexture2D(&t,nullptr,&depth_texture))||FAILED(device->CreateDepthStencilView(depth_texture,nullptr,&depth_view))) {
        drop(depth_view);drop(depth_texture);return false;
    }
    depth_width=width;depth_height=height;return true;
}
bool ensure_vertices(ID3D11Device* device,UINT count) {
    if(vertices&&vertex_capacity>=count)return true;
    drop(vertices);vertex_capacity=0;
    UINT capacity=4096;while(capacity<count)capacity*=2;
    D3D11_BUFFER_DESC b{};b.ByteWidth=capacity*sizeof(Vertex);b.Usage=D3D11_USAGE_DYNAMIC;b.BindFlags=D3D11_BIND_VERTEX_BUFFER;b.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    if(FAILED(device->CreateBuffer(&b,nullptr,&vertices)))return false;
    vertex_capacity=capacity;return true;
}
// A shader-readable view of the scene depth, or null when its format is not decoded.
ID3D11ShaderResourceView* readable_scene(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11DepthStencilView* dsv) {
    ID3D11Resource* resource=nullptr;dsv->GetResource(&resource);if(!resource)return nullptr;
    ID3D11Texture2D* texture=nullptr;
    if(FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&texture)))) {drop(resource);return nullptr;}
    drop(resource);
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    DXGI_FORMAT view_format=DXGI_FORMAT_UNKNOWN;
    if(desc.Format==DXGI_FORMAT_R32G8X24_TYPELESS||desc.Format==DXGI_FORMAT_D32_FLOAT_S8X24_UINT)view_format=DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    else if(desc.Format==DXGI_FORMAT_R24G8_TYPELESS||desc.Format==DXGI_FORMAT_D24_UNORM_S8_UINT)view_format=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    else if(desc.Format==DXGI_FORMAT_R32_TYPELESS||desc.Format==DXGI_FORMAT_D32_FLOAT)view_format=DXGI_FORMAT_R32_FLOAT;
    const bool typeless=desc.Format==DXGI_FORMAT_R32G8X24_TYPELESS||desc.Format==DXGI_FORMAT_R24G8_TYPELESS||desc.Format==DXGI_FORMAT_R32_TYPELESS;
    if(view_format==DXGI_FORMAT_UNKNOWN||desc.SampleDesc.Count!=1||desc.ArraySize!=1) {drop(texture);return nullptr;}
    const bool direct=(desc.BindFlags&D3D11_BIND_SHADER_RESOURCE)&&typeless;
    if(scene_source!=texture||std::memcmp(&scene_desc,&desc,sizeof(desc))) {
        drop(scene_view);drop(scene_copy);drop(scene_source);scene_desc=desc;scene_source=texture;scene_source->AddRef();
        D3D11_SHADER_RESOURCE_VIEW_DESC v{};v.Format=view_format;v.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;v.Texture2D.MipLevels=1;
        if(direct) {
            if(FAILED(device->CreateShaderResourceView(texture,&v,&scene_view)))drop(scene_view);
        } else if(typeless) {
            auto copy=desc;copy.BindFlags=D3D11_BIND_SHADER_RESOURCE;copy.Usage=D3D11_USAGE_DEFAULT;copy.CPUAccessFlags=0;copy.MiscFlags=0;
            if(SUCCEEDED(device->CreateTexture2D(&copy,nullptr,&scene_copy))&&FAILED(device->CreateShaderResourceView(scene_copy,&v,&scene_view)))drop(scene_view);
        }
    }
    if(scene_view&&scene_copy)context->CopyResource(scene_copy,texture);
    drop(texture);
    return scene_view;
}
// Everything draw() changes, other than the output-merger targets.
struct State {
    UINT viewport_count=D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    ID3D11RasterizerState* raster=nullptr;
    ID3D11BlendState* blend=nullptr;float blend_factor[4]{};UINT sample_mask=0;
    ID3D11DepthStencilState* depth=nullptr;UINT stencil_ref=0;
    ID3D11InputLayout* layout=nullptr;D3D11_PRIMITIVE_TOPOLOGY topology{};
    ID3D11Buffer* buffer=nullptr;UINT stride=0,offset=0;
    ID3D11VertexShader* vs=nullptr;ID3D11PixelShader* ps=nullptr;ID3D11GeometryShader* gs=nullptr;ID3D11HullShader* hs=nullptr;ID3D11DomainShader* ds=nullptr;
    ID3D11ClassInstance* vs_instances[256]{};ID3D11ClassInstance* ps_instances[256]{};ID3D11ClassInstance* gs_instances[256]{};
    UINT vs_count=256,ps_count=256,gs_count=256;
    ID3D11ShaderResourceView* ps_resource=nullptr;ID3D11Buffer* ps_constants=nullptr;
    ID3D11Buffer* index_buffer=nullptr;DXGI_FORMAT index_format{};UINT index_offset=0;
    ID3D11SamplerState* ps_sampler=nullptr;ID3D11ShaderResourceView* ps_resource1=nullptr;ID3D11Buffer* vs_constants=nullptr;
    UINT scissor_count=D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;D3D11_RECT scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    void save(ID3D11DeviceContext* c) {
        c->RSGetViewports(&viewport_count,viewports);c->RSGetState(&raster);
        c->OMGetBlendState(&blend,blend_factor,&sample_mask);c->OMGetDepthStencilState(&depth,&stencil_ref);
        c->IAGetInputLayout(&layout);c->IAGetPrimitiveTopology(&topology);c->IAGetVertexBuffers(0,1,&buffer,&stride,&offset);
        c->VSGetShader(&vs,vs_instances,&vs_count);c->PSGetShader(&ps,ps_instances,&ps_count);c->GSGetShader(&gs,gs_instances,&gs_count);
        c->HSGetShader(&hs,nullptr,nullptr);c->DSGetShader(&ds,nullptr,nullptr);
        c->PSGetShaderResources(0,1,&ps_resource);c->PSGetConstantBuffers(0,1,&ps_constants);
        c->IAGetIndexBuffer(&index_buffer,&index_format,&index_offset);c->PSGetSamplers(0,1,&ps_sampler);
        c->PSGetShaderResources(1,1,&ps_resource1);c->RSGetScissorRects(&scissor_count,scissors);c->VSGetConstantBuffers(0,1,&vs_constants);
    }
    void restore(ID3D11DeviceContext* c) {
        c->RSSetViewports(viewport_count,viewports);c->RSSetState(raster);
        c->OMSetBlendState(blend,blend_factor,sample_mask);c->OMSetDepthStencilState(depth,stencil_ref);
        c->IASetInputLayout(layout);c->IASetPrimitiveTopology(topology);c->IASetVertexBuffers(0,1,&buffer,&stride,&offset);
        c->VSSetShader(vs,vs_instances,vs_count);c->PSSetShader(ps,ps_instances,ps_count);c->GSSetShader(gs,gs_instances,gs_count);
        c->HSSetShader(hs,nullptr,0);c->DSSetShader(ds,nullptr,0);
        c->PSSetShaderResources(0,1,&ps_resource);c->PSSetConstantBuffers(0,1,&ps_constants);
        c->IASetIndexBuffer(index_buffer,index_format,index_offset);c->PSSetSamplers(0,1,&ps_sampler);
        c->PSSetShaderResources(1,1,&ps_resource1);c->RSSetScissorRects(scissor_count,scissors);
        c->VSSetConstantBuffers(0,1,&vs_constants);
        collision_gpu::drop(index_buffer);collision_gpu::drop(ps_sampler);collision_gpu::drop(ps_resource1);collision_gpu::drop(vs_constants);
        collision_gpu::drop(raster);collision_gpu::drop(blend);collision_gpu::drop(depth);collision_gpu::drop(layout);collision_gpu::drop(buffer);
        collision_gpu::drop(vs);collision_gpu::drop(ps);collision_gpu::drop(gs);collision_gpu::drop(hs);collision_gpu::drop(ds);
        collision_gpu::drop(ps_resource);collision_gpu::drop(ps_constants);
        for(UINT i=0;i<vs_count;++i)collision_gpu::drop(vs_instances[i]);
        for(UINT i=0;i<ps_count;++i)collision_gpu::drop(ps_instances[i]);
        for(UINT i=0;i<gs_count;++i)collision_gpu::drop(gs_instances[i]);
    }
};
// The scene-depth test's constants: camera, lens, camera viewport in target pixels,
// scene texels per target pixel and the zombie boxes.
void fill_occlusion(Constants& values,const Occlusion& o,float x,float y,float w,float h,unsigned width,unsigned height) {
    for(int i=0;i<3;++i) {values.origin[i]=o.origin[i];values.forward[i]=o.forward[i];values.left[i]=o.left[i];values.up[i]=o.up[i];}
    values.lens[0]=o.tan_x;values.lens[1]=o.tan_y;values.lens[2]=o.near_plane;values.lens[3]=o.weapon_distance;
    values.port[0]=x;values.port[1]=y;values.port[2]=w;values.port[3]=h;
    const int boxes=std::min(std::max(o.box_count,0),Occlusion::max_boxes);
    values.test[0]=o.tolerance;values.test[1]=o.relative;values.test[2]=1;values.test[3]=static_cast<float>(boxes);
    values.scale[0]=static_cast<float>(scene_desc.Width)/static_cast<float>(width);values.scale[1]=static_cast<float>(scene_desc.Height)/static_cast<float>(height);
    for(int b=0;b<boxes;++b)for(int k=0;k<3;++k) {values.boxes[b*2][k]=o.boxes[b][k];values.boxes[b*2+1][k]=o.boxes[b][3+k];}
}
}
bool ready(ID3D11Device* device) {
    if(!device)return false;
    if(owner!=device) {release();owner=device;failed=!create(device);}
    return !failed;
}
bool draw(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11RenderTargetView* target,unsigned width,unsigned height,const Batch& batch,const Occlusion* occlusion) {
    if(!ready(device)||batch.empty()||!target||!width||!height)return false;
    // One vertex buffer: fills, lines, overlay fills, overlay lines.
    const std::vector<Vertex>* parts[]={&batch.fills,&batch.lines,&batch.overlay_fills,&batch.overlay_lines};
    UINT total=0;for(const auto* part:parts)total+=static_cast<UINT>(part->size());
    if(!ensure_depth(device,width,height)||!ensure_vertices(device,total))return false;
    ID3D11ShaderResourceView* scene=nullptr;
    if(occlusion&&occlusion->scene)scene=readable_scene(device,context,occlusion->scene);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if(FAILED(context->Map(vertices,0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return false;
    UINT first[4]{};UINT at=0;
    for(int i=0;i<4;++i) {
        first[i]=at;
        if(!parts[i]->empty())std::memcpy(static_cast<Vertex*>(mapped.pData)+at,parts[i]->data(),parts[i]->size()*sizeof(Vertex));
        at+=static_cast<UINT>(parts[i]->size());
    }
    context->Unmap(vertices,0);
    Constants values;
    if(scene)fill_occlusion(values,*occlusion,batch.viewport_x,batch.viewport_y,batch.viewport_width,batch.viewport_height,width,height);
    if(FAILED(context->Map(constants,0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return false;
    std::memcpy(mapped.pData,&values,sizeof(values));context->Unmap(constants,0);
    State saved;saved.save(context);
    context->OMSetRenderTargets(1,&target,depth_view);
    context->ClearDepthStencilView(depth_view,D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1.0f,0);
    const D3D11_VIEWPORT viewport{batch.viewport_x,batch.viewport_y,batch.viewport_width,batch.viewport_height,0,1};
    context->RSSetViewports(1,&viewport);context->RSSetState(raster);
    const UINT stride=sizeof(Vertex),offset=0;
    context->IASetInputLayout(layout);context->IASetVertexBuffers(0,1,&vertices,&stride,&offset);
    context->VSSetShader(vertex_shader,nullptr,0);context->PSSetShader(pixel_shader,nullptr,0);
    context->GSSetShader(nullptr,nullptr,0);context->HSSetShader(nullptr,nullptr,0);context->DSSetShader(nullptr,nullptr,0);
    context->PSSetConstantBuffers(0,1,&constants);context->PSSetShaderResources(0,1,&scene);
    const float factor[4]{};
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    // Pass 1: nearest depth only. Pass 2: colour exactly that nearest surface.
    if(!batch.fills.empty()) {
        context->OMSetBlendState(no_color,factor,0xFFFFFFFF);context->OMSetDepthStencilState(depth_write,0);
        context->Draw(static_cast<UINT>(batch.fills.size()),first[0]);
    }
    context->OMSetBlendState(alpha_blend,factor,0xFFFFFFFF);context->OMSetDepthStencilState(depth_test,0);
    if(!batch.fills.empty())context->Draw(static_cast<UINT>(batch.fills.size()),first[0]);
    if(!batch.lines.empty()) {
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        context->Draw(static_cast<UINT>(batch.lines.size()),first[1]);
    }
    // Overlay (NAV graph): no depth test against the collision, links under nodes.
    context->OMSetDepthStencilState(depth_off,0);
    if(!batch.overlay_lines.empty()) {
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        context->Draw(static_cast<UINT>(batch.overlay_lines.size()),first[3]);
    }
    if(!batch.overlay_fills.empty()) {
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->Draw(static_cast<UINT>(batch.overlay_fills.size()),first[2]);
    }
    ID3D11ShaderResourceView* none=nullptr;context->PSSetShaderResources(0,1,&none);
    saved.restore(context);
    return scene!=nullptr;
}
bool draw_list(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11RenderTargetView* target,unsigned width,unsigned height,
    const ImDrawList& list,const float camera_viewport[4],const Occlusion* occlusion) {
    if(!ready(device)||!target||!width||!height||list.VtxBuffer.empty()||list.IdxBuffer.empty())return false;
    const auto vertex_count=static_cast<UINT>(list.VtxBuffer.Size),index_count=static_cast<UINT>(list.IdxBuffer.Size);
    if(!list_vertices||list_vertex_capacity<vertex_count) {
        drop(list_vertices);list_vertex_capacity=0;UINT capacity=8192;while(capacity<vertex_count)capacity*=2;
        D3D11_BUFFER_DESC b{};b.ByteWidth=capacity*sizeof(ImDrawVert);b.Usage=D3D11_USAGE_DYNAMIC;b.BindFlags=D3D11_BIND_VERTEX_BUFFER;b.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        if(FAILED(device->CreateBuffer(&b,nullptr,&list_vertices)))return false;
        list_vertex_capacity=capacity;
    }
    if(!list_indices||list_index_capacity<index_count) {
        drop(list_indices);list_index_capacity=0;UINT capacity=16384;while(capacity<index_count)capacity*=2;
        D3D11_BUFFER_DESC b{};b.ByteWidth=capacity*sizeof(ImDrawIdx);b.Usage=D3D11_USAGE_DYNAMIC;b.BindFlags=D3D11_BIND_INDEX_BUFFER;b.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        if(FAILED(device->CreateBuffer(&b,nullptr,&list_indices)))return false;
        list_index_capacity=capacity;
    }
    ID3D11ShaderResourceView* scene=nullptr;
    if(occlusion&&occlusion->scene)scene=readable_scene(device,context,occlusion->scene);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if(FAILED(context->Map(list_vertices,0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return false;
    std::memcpy(mapped.pData,list.VtxBuffer.Data,vertex_count*sizeof(ImDrawVert));context->Unmap(list_vertices,0);
    if(FAILED(context->Map(list_indices,0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return false;
    std::memcpy(mapped.pData,list.IdxBuffer.Data,index_count*sizeof(ImDrawIdx));context->Unmap(list_indices,0);
    Constants values;
    if(scene)fill_occlusion(values,*occlusion,camera_viewport[0],camera_viewport[1],camera_viewport[2],camera_viewport[3],width,height);
    values.scale[2]=static_cast<float>(width);values.scale[3]=static_cast<float>(height);  // pixels to clip space
    if(FAILED(context->Map(constants,0,D3D11_MAP_WRITE_DISCARD,0,&mapped)))return false;
    std::memcpy(mapped.pData,&values,sizeof(values));context->Unmap(constants,0);
    State saved;saved.save(context);
    context->OMSetRenderTargets(1,&target,nullptr);
    const D3D11_VIEWPORT viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1};
    context->RSSetViewports(1,&viewport);context->RSSetState(list_raster);
    const UINT stride=sizeof(ImDrawVert),offset=0;
    context->IASetInputLayout(list_layout);context->IASetVertexBuffers(0,1,&list_vertices,&stride,&offset);
    context->IASetIndexBuffer(list_indices,sizeof(ImDrawIdx)==2?DXGI_FORMAT_R16_UINT:DXGI_FORMAT_R32_UINT,0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(list_vertex_shader,nullptr,0);context->PSSetShader(list_pixel_shader,nullptr,0);
    context->GSSetShader(nullptr,nullptr,0);context->HSSetShader(nullptr,nullptr,0);context->DSSetShader(nullptr,nullptr,0);
    context->VSSetConstantBuffers(0,1,&constants);context->PSSetConstantBuffers(0,1,&constants);
    context->PSSetShaderResources(0,1,&scene);context->PSSetSamplers(0,1,&list_sampler);
    const float factor[4]{};
    context->OMSetBlendState(alpha_blend,factor,0xFFFFFFFF);context->OMSetDepthStencilState(depth_off,0);
    for(const auto& command:list.CmdBuffer) {
        if(command.UserCallback||!command.ElemCount)continue;
        const D3D11_RECT clip{static_cast<LONG>(command.ClipRect.x),static_cast<LONG>(command.ClipRect.y),static_cast<LONG>(command.ClipRect.z),static_cast<LONG>(command.ClipRect.w)};
        if(clip.right<=clip.left||clip.bottom<=clip.top)continue;
        context->RSSetScissorRects(1,&clip);
        auto* texture=reinterpret_cast<ID3D11ShaderResourceView*>(command.GetTexID());
        context->PSSetShaderResources(1,1,&texture);
        context->DrawIndexed(command.ElemCount,command.IdxOffset,static_cast<INT>(command.VtxOffset));
    }
    ID3D11ShaderResourceView* none[2]{};context->PSSetShaderResources(0,2,none);
    saved.restore(context);
    return true;
}
void release() {
    drop(vertex_shader);drop(pixel_shader);drop(layout);drop(vertices);vertex_capacity=0;drop(constants);
    drop(list_vertex_shader);drop(list_pixel_shader);drop(list_layout);drop(list_vertices);list_vertex_capacity=0;
    drop(list_indices);list_index_capacity=0;drop(list_sampler);drop(list_raster);
    drop(depth_view);drop(depth_texture);depth_width=depth_height=0;
    drop(depth_write);drop(depth_test);drop(depth_off);drop(no_color);drop(alpha_blend);drop(raster);
    drop(scene_view);drop(scene_copy);drop(scene_source);scene_desc={};
    owner=nullptr;failed=false;
}
}
