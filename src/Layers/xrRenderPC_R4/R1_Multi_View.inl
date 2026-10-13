// Render-owner implementation; included in r4_R_render.cpp.
#include "../xrRender/ResourceManager.h"
#include "../xrRender/dxRenderDeviceRender.h"
#include "../xrRender/DetailManager.h"
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <cmath>
#include <memory>

namespace r1mv
{
using Microsoft::WRL::ComPtr;
constexpr u32 count = 8;
constexpr u32 poolCount = 3;
struct Slot
{
    R1ViewParameters p;
    R1ViewStatus status;
    ref_rt image;
    R1ViewGeometry geometry;
    ref_texture materials[2];
    Fmatrix source;
    u32 touch=0;
    bool active=false;
};
Slot slots[count];
bool busy=false;
u32 poolSerial=0;

struct Pair { CRT* main; ref_rt scratch; };
struct Pool
{
    u32 width=0,height=0;
    xr_vector<Pair> pairs;
    ref_rt color,depth;
    ref_texture depthName;
    bool initialize(u32 w,u32 h)
    {
        width=w;height=h;auto main=static_cast<dxRenderDeviceRender*>(Device.m_pRender)->Resources->R1ScreenTargets(Device.dwWidth,Device.dwHeight);
        const u32 serial=++poolSerial;
        for(CRT* rt:main)
        {
            D3D11_TEXTURE2D_DESC d;rt->pSurface->GetDesc(&d);
            if(d.SampleDesc.Count!=1)return false;
            string128 name;xr_sprintf(name,"$user$r1mv_scratch_%u_%u",serial,u32(pairs.size()));
            Pair entry;entry.main=rt;
            u32 rw=_max(1u,u32(u64(rt->dwWidth)*w/Device.dwWidth));
            u32 rh=_max(1u,u32(u64(rt->dwHeight)*h/Device.dwHeight));
            entry.scratch.create(name,rw,rh,rt->fmt,1,rt->pUAView!=nullptr);
            pairs.push_back(entry);
        }
        string128 name;xr_sprintf(name,"$user$r1mv_base_%u",serial);
        color.create(name,w,h,D3DFMT_A8R8G8B8);
        xr_sprintf(name,"$user$r1mv_depth_%u",serial);depth.create(name,w,h,D3DFMT_D24S8);
        depthName.create("$user$depth");
        return color && depth;
    }
    void swap()
    {
        for(auto& p:pairs)
        {
            CRT& a=*p.main;CRT& b=*p.scratch;
            std::swap(a.pSurface,b.pSurface);std::swap(a.pRT,b.pRT);std::swap(a.pZRT,b.pZRT);
            std::swap(a.pUAView,b.pUAView);std::swap(a.dwWidth,b.dwWidth);std::swap(a.dwHeight,b.dwHeight);
            a.pTexture->R1SwapSurface(*b.pTexture);
        }
        depthName->R1SwapSurface(*depth->pTexture);
    }
};
// One bounded working set per requested resolution. Outputs are retained per
// view; transient G-buffer resources are reused across same-size views.
std::unique_ptr<Pool> pools[poolCount];
Pool* getPool(u32 w,u32 h)
{
    for(auto& p:pools)if(p&&p->width==w&&p->height==h)return p.get();
    for(auto& p:pools)if(!p){p=std::make_unique<Pool>();if(!p->initialize(w,h)){p.reset();return nullptr;}return p.get();}
    // Settings changes cannot accumulate an unbounded GPU cache.
    pools[0].reset();pools[0]=std::make_unique<Pool>();
    if(!pools[0]->initialize(w,h)){pools[0].reset();return nullptr;}return pools[0].get();
}
bool live(const Slot& s){return s.active && u32(Device.dwTimeContinual-s.touch)<1500;}
Fvector4 transform(const Fvector& p,const Fmatrix& m)
{
    Fvector4 r;
    r.set(p.x*m._11+p.y*m._21+p.z*m._31+m._41,
          p.x*m._12+p.y*m._22+p.z*m._32+m._42,
          p.x*m._13+p.y*m._23+p.z*m._33+m._43,
          p.x*m._14+p.y*m._24+p.z*m._34+m._44);return r;
}
bool visible(const Slot& s)
{
    if(s.p.output!=2)return true;
    Fvector4 corners[4];
    for(u32 i=0;i<4;++i)
    {
        Fvector p=s.p.quad_position;p.mad(s.p.quad_right,(i&1)?1.f:-1.f);p.mad(s.p.quad_up,(i&2)?1.f:-1.f);
        corners[i]=transform(p,Device.mFullTransform);
    }
    for(u32 plane=0;plane<7;++plane)
    {
        bool outside=true;
        for(const auto& p:corners)
        {
            float value=plane==0?p.w-.001f:plane==1?p.x+p.w:plane==2?p.w-p.x:plane==3?p.y+p.w:plane==4?p.w-p.y:plane==5?p.z:p.w-p.z;
            if(value>=0)outside=false;
        }
        if(outside)return false;
    }
    return true;
}
struct Camera
{
    Fvector position=Device.vCameraPosition,direction=Device.vCameraDirection,top=Device.vCameraTop,right=Device.vCameraRight;
    Fmatrix view=Device.mView,projection=Device.mProject,hud=Device.mProjectHud,full=Device.mFullTransform,fullHud=Device.mFullTransformHud;
    Fmatrix inverseView=Device.mInvView,inverseProjection=Device.mInvProject,inverseHud=Device.mInvProjectHud,inverseFull=Device.mInvFullTransform;
    Fmatrix previousView=Device.mView_prev,previousProjection=Device.mProject_prev;
    float fov=Device.fFOV,aspect=Device.fASPECT;
    void apply(const R1ViewParameters& p)
    {
        Device.vCameraPosition=p.relative?position:p.position;
        Device.vCameraPosition.mad(right,p.offset.x).mad(top,p.offset.y).mad(direction,p.offset.z);
        Device.vCameraDirection=p.relative?direction:p.direction;
        Device.vCameraTop=p.relative?top:p.up;
        if(p.transform_camera)
        {
            p.camera_transform.transform_tiny(Device.vCameraPosition);
            p.camera_transform.transform_dir(Device.vCameraDirection);
            p.camera_transform.transform_dir(Device.vCameraTop);
        }
        Device.vCameraDirection.normalize();Device.vCameraTop.normalize();
        Device.vCameraRight.crossproduct(Device.vCameraTop,Device.vCameraDirection).normalize();
        Device.vCameraTop.crossproduct(Device.vCameraDirection,Device.vCameraRight).normalize();
        Device.mView.build_camera_dir(Device.vCameraPosition,Device.vCameraDirection,Device.vCameraTop);
        Device.fFOV=p.fov>0?p.fov:fov;
        Device.fASPECT=p.aspect>0?p.aspect:float(p.height)/float(p.width);
        float farPlane=projection._33>1?projection._43/(1-projection._33):500.f;
        if(p.far_distance>0)farPlane=_min(farPlane,p.far_distance);
        Device.mProject.build_projection(deg2rad(Device.fFOV),Device.fASPECT,.08f,_max(1.f,farPlane));
        Device.mProject._31+=p.projection_shift;
        Device.mProjectHud=hud;Device.mProjectHud._11*=Device.fASPECT/aspect;
        Device.mProjectHud._31+=p.projection_shift;
        // Keep ordinary projection signs: stock sky/deferred/SSFX shaders assume
        // them. Mirrored output is sampled horizontally flipped by the compositor.
        if(p.clip)
        {
            using namespace DirectX;
            auto v=XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(&Device.mView));
            auto m=XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(&Device.mProject));
            auto plane=XMVector4Transform(XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(&p.plane)),XMMatrixTranspose(XMMatrixInverse(nullptr,v)));
            XMFLOAT4 qplane;XMStoreFloat4(&qplane,plane);
            auto q=XMVector4Transform(XMVectorSet(qplane.x>=0?1.f:-1.f,qplane.y>=0?1.f:-1.f,1,1),XMMatrixInverse(nullptr,m));
            float den=XMVectorGetX(XMVector4Dot(plane,q));
            if(std::isfinite(den)&&fabsf(den)>.0001f)
            {
                Device.mProject._13=qplane.x/den;Device.mProject._23=qplane.y/den;
                Device.mProject._33=qplane.z/den;Device.mProject._43=qplane.w/den;
            }
        }
        Device.mFullTransform.mul(Device.mProject,Device.mView);Device.mFullTransformHud.mul(Device.mProjectHud,Device.mView);
        Device.mInvView.invert(Device.mView);Device.mInvProject.invert(Device.mProject);
        Device.mInvProjectHud.invert(Device.mProjectHud);Device.mInvFullTransform.invert(Device.mFullTransform);
        // Render-only contexts have no previous-view temporal history in v1.
        Device.mView_prev=Device.mView;Device.mProject_prev=Device.mProject;
        Device.m_pRender->SetCacheXform(Device.mView,Device.mProject);
        Device.m_pRender->SetCacheXform_prev(Device.mView_prev,Device.mProject_prev);
    }
    void restore()
    {
        Device.vCameraPosition=position;Device.vCameraDirection=direction;Device.vCameraTop=top;Device.vCameraRight=right;
        Device.mView=view;Device.mProject=projection;Device.mProjectHud=hud;Device.mFullTransform=full;Device.mFullTransformHud=fullHud;
        Device.mInvView=inverseView;Device.mInvProject=inverseProjection;Device.mInvProjectHud=inverseHud;Device.mInvFullTransform=inverseFull;
        Device.mView_prev=previousView;Device.mProject_prev=previousProjection;Device.fFOV=fov;Device.fASPECT=aspect;
        Device.m_pRender->SetCacheXform(view,projection);Device.m_pRender->SetCacheXform_prev(previousView,previousProjection);
    }
    ~Camera(){restore();}
};
struct Vertex {float x,y,z,w,u,v,q,material;};
struct GPU
{
    bool initialized=false;
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps,uiPS;
    ComPtr<ID3D11InputLayout> layout;ComPtr<ID3D11Buffer> vertices;
    ComPtr<ID3D11SamplerState> sampler;ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11DepthStencilState> depth,overlay;ComPtr<ID3D11BlendState> blend,uiBlend;
    bool initialize()
    {
        if(initialized)return true;
        *this=GPU{};
        const char* text="Texture2D image:register(t0);Texture2D material1:register(t1);Texture2D material2:register(t2);SamplerState filter:register(s0);struct V{float4 p:POSITION;float3 uv:TEXCOORD0;float material:TEXCOORD1;};struct O{float4 p:SV_POSITION;float3 uv:TEXCOORD0;nointerpolation float material:TEXCOORD1;};O vertex(V v){O o;o.p=v.p;o.uv=v.uv;o.material=v.material;return o;}float4 pixel(O v):SV_TARGET{if(v.uv.z<=.001)discard;float2 uv=v.uv.xy/v.uv.z;if(v.material<.5&&any(uv<0)||v.material<.5&&any(uv>1))return float4(.015,.018,.016,1);float3 c=v.material<.5?image.Sample(filter,uv).rgb:v.material<1.5?material1.Sample(filter,uv).rgb:material2.Sample(filter,uv).rgb;return float4(c,1);}float4 ui_pixel(O v):SV_TARGET{return image.Sample(filter,v.uv.xy/v.uv.z);}";
        ComPtr<ID3DBlob> v,p,error;
        if(FAILED(D3DCompile(text,strlen(text),"R1_Multi_View",nullptr,nullptr,"vertex","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&v,&error)))return false;
        if(FAILED(D3DCompile(text,strlen(text),"R1_Multi_View",nullptr,nullptr,"pixel","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&p,&error)))return false;
        if(FAILED(HW.pDevice->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs)))return false;
        if(FAILED(HW.pDevice->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&ps)))return false;
        p.Reset();
        if(FAILED(D3DCompile(text,strlen(text),"R1_Multi_View_UI",nullptr,nullptr,"ui_pixel","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&p,&error)))return false;
        if(FAILED(HW.pDevice->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&uiPS)))return false;
        D3D11_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",0,DXGI_FORMAT_R32G32B32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",1,DXGI_FORMAT_R32_FLOAT,0,28,D3D11_INPUT_PER_VERTEX_DATA,0}};
        if(FAILED(HW.pDevice->CreateInputLayout(elements,3,v->GetBufferPointer(),v->GetBufferSize(),&layout)))return false;
        D3D11_BUFFER_DESC b{};b.ByteWidth=256*sizeof(Vertex);b.Usage=D3D11_USAGE_DYNAMIC;b.BindFlags=D3D11_BIND_VERTEX_BUFFER;b.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        if(FAILED(HW.pDevice->CreateBuffer(&b,nullptr,&vertices)))return false;
        D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;s.MaxLOD=D3D11_FLOAT32_MAX;
        if(FAILED(HW.pDevice->CreateSamplerState(&s,&sampler)))return false;
        D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=true;
        if(FAILED(HW.pDevice->CreateRasterizerState(&r,&raster)))return false;
        D3D11_DEPTH_STENCIL_DESC d{};d.DepthEnable=true;d.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;d.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
        if(FAILED(HW.pDevice->CreateDepthStencilState(&d,&depth)))return false;
        d.DepthEnable=false;if(FAILED(HW.pDevice->CreateDepthStencilState(&d,&overlay)))return false;
        D3D11_BLEND_DESC bl{};bl.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        if(FAILED(HW.pDevice->CreateBlendState(&bl,&blend)))return false;
        auto& target=bl.RenderTarget[0];target.BlendEnable=true;target.SrcBlend=D3D11_BLEND_SRC_ALPHA;target.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;target.BlendOp=D3D11_BLEND_OP_ADD;
        target.SrcBlendAlpha=D3D11_BLEND_ONE;target.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;target.BlendOpAlpha=D3D11_BLEND_OP_ADD;
        initialized=SUCCEEDED(HW.pDevice->CreateBlendState(&bl,&uiBlend));return initialized;
    }
    void draw(Slot& slot,bool world,const Fvector4* rectangle=nullptr,bool ui=false)
    {
        if(!slot.image||!slot.status.ready||slot.status.error)return;
        if(!initialize()){slot.status.error=-5;Msg("! [R1 Multi View] compositor initialization failed");return;}
        Vertex v[256];const u32 order[]={0,1,2,2,1,3};
        const u32 drawCount=world&&!slot.geometry.vertices.empty()?u32(slot.geometry.vertices.size()):6;
        for(u32 i=0;i<drawCount;++i)
        {
            if(world&&!slot.geometry.vertices.empty())
            {
                const auto& input=slot.geometry.vertices[i];auto p=transform(input.position,Device.mFullTransform);
                auto uv=transform(input.position,slot.source);
                v[i]={p.x,p.y,p.z,p.w,input.material?input.uv.x:(uv.x+uv.w)*.5f,input.material?input.uv.y:(uv.w-uv.y)*.5f,input.material?1.f:uv.w,float(input.material)};
                continue;
            }
            u32 index=order[i];float u=(index&1)?1.f:0.f, t=(index&2)?0.f:1.f;
            Fvector4 p,uv;
            if(world)
            {
                Fvector point=slot.p.quad_position;point.mad(slot.p.quad_right,u*2-1).mad(slot.p.quad_up,(1-t)*2-1);
                p=transform(point,Device.mFullTransform);uv=transform(point,slot.source);
                v[i]={p.x,p.y,p.z,p.w,(uv.x+uv.w)*.5f,(uv.w-uv.y)*.5f,uv.w,0};
            }
            else
            {
                const auto& r=rectangle?*rectangle:slot.p.rect;
                v[i]={2*(r.x+u*r.z)-1,1-2*(r.y+t*r.w),0,1,u,t,1,0};
            }
        }
        if(slot.p.flip_x && !ui)
            for(u32 i=0;i<drawCount;++i)
                if(v[i].material<.5f)v[i].u=v[i].q-v[i].u;
        auto ctx=HW.pContext;D3D11_MAPPED_SUBRESOURCE data;
        // u_setrt queues a cache change. Direct native draws need the intended
        // base target bound now, before the next ordinary cached draw applies it.
        ctx->OMSetRenderTargets(1,&HW.pBaseRT,HW.pBaseZB);
        UINT viewportCount=D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        D3D11_VIEWPORT oldViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        ctx->RSGetViewports(&viewportCount,oldViewports);
        D3D11_VIEWPORT viewport={0,0,float(Device.dwWidth),float(Device.dwHeight),0,1};ctx->RSSetViewports(1,&viewport);
        if(FAILED(ctx->Map(vertices.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&data)))return;
        memcpy(data.pData,v,drawCount*sizeof(Vertex));ctx->Unmap(vertices.Get(),0);
        // Restore every pipeline component changed by this native compositor;
        // the renderer's cached state remains consistent for the next UI draw.
        ComPtr<ID3D11VertexShader> oldVS;ComPtr<ID3D11PixelShader> oldPS;ComPtr<ID3D11GeometryShader> oldGS;
        ComPtr<ID3D11HullShader> oldHS;ComPtr<ID3D11DomainShader> oldDS;ComPtr<ID3D11InputLayout> oldLayout;
        ComPtr<ID3D11Buffer> oldVB;UINT stride,offset;D3D11_PRIMITIVE_TOPOLOGY topology;
        ID3D11ShaderResourceView* oldSRV[3]={};ComPtr<ID3D11SamplerState> oldSampler;
        ComPtr<ID3D11RasterizerState> oldRaster;ComPtr<ID3D11DepthStencilState> oldDepth;UINT reference;
        ComPtr<ID3D11BlendState> oldBlend;FLOAT factor[4];UINT mask;
        ctx->VSGetShader(&oldVS,nullptr,nullptr);ctx->PSGetShader(&oldPS,nullptr,nullptr);ctx->GSGetShader(&oldGS,nullptr,nullptr);
        ctx->HSGetShader(&oldHS,nullptr,nullptr);ctx->DSGetShader(&oldDS,nullptr,nullptr);
        ctx->IAGetInputLayout(&oldLayout);ctx->IAGetVertexBuffers(0,1,&oldVB,&stride,&offset);ctx->IAGetPrimitiveTopology(&topology);
        ctx->PSGetShaderResources(0,3,oldSRV);ctx->PSGetSamplers(0,1,&oldSampler);ctx->RSGetState(&oldRaster);
        ctx->OMGetDepthStencilState(&oldDepth,&reference);ctx->OMGetBlendState(&oldBlend,factor,&mask);
        ctx->VSSetShader(vs.Get(),nullptr,0);ctx->PSSetShader(ui?uiPS.Get():ps.Get(),nullptr,0);ctx->GSSetShader(nullptr,nullptr,0);
        ctx->HSSetShader(nullptr,nullptr,0);ctx->DSSetShader(nullptr,nullptr,0);ctx->IASetInputLayout(layout.Get());
        ID3D11Buffer* vb=vertices.Get();UINT st=sizeof(Vertex),of=0;ctx->IASetVertexBuffers(0,1,&vb,&st,&of);ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11ShaderResourceView* resources[3]={slot.image->pTexture->get_SRView(),slot.materials[0]?slot.materials[0]->get_SRView():nullptr,slot.materials[1]?slot.materials[1]->get_SRView():nullptr};ctx->PSSetShaderResources(0,3,resources);
        ID3D11SamplerState* sam=sampler.Get();ctx->PSSetSamplers(0,1,&sam);ctx->RSSetState(raster.Get());
        ctx->OMSetDepthStencilState(world?depth.Get():overlay.Get(),0);ctx->OMSetBlendState(ui?uiBlend.Get():blend.Get(),nullptr,0xffffffff);ctx->Draw(drawCount,0);slot.status.draws++;
        ctx->VSSetShader(oldVS.Get(),nullptr,0);ctx->PSSetShader(oldPS.Get(),nullptr,0);ctx->GSSetShader(oldGS.Get(),nullptr,0);
        ctx->HSSetShader(oldHS.Get(),nullptr,0);ctx->DSSetShader(oldDS.Get(),nullptr,0);ctx->IASetInputLayout(oldLayout.Get());
        vb=oldVB.Get();ctx->IASetVertexBuffers(0,1,&vb,&stride,&offset);ctx->IASetPrimitiveTopology(topology);
        ctx->PSSetShaderResources(0,3,oldSRV);for(auto p:oldSRV)if(p)p->Release();sam=oldSampler.Get();ctx->PSSetSamplers(0,1,&sam);ctx->RSSetState(oldRaster.Get());
        ctx->OMSetDepthStencilState(oldDepth.Get(),reference);ctx->OMSetBlendState(oldBlend.Get(),factor,mask);ctx->RSSetViewports(viewportCount,oldViewports);
    }
};
GPU gpu;
Slot uiCanvas;
bool uiPending=false;
u32 screenFrame=u32(-1);
}

bool CRender::R1ViewSet(u32 id,const R1ViewParameters& p)
{
    using namespace r1mv;
    if(busy||id>=count||p.width<64||p.height<64||p.width>4096||p.height>4096||p.interval<1||p.interval>120||p.quality>2||p.output>2)return false;
    const Fvector* vectors[]={&p.position,&p.direction,&p.up,&p.offset,&p.quad_position,&p.quad_right,&p.quad_up};
    for(auto v:vectors)if(!std::isfinite(v->x)||!std::isfinite(v->y)||!std::isfinite(v->z)||fabsf(v->x)>1e6f||fabsf(v->y)>1e6f||fabsf(v->z)>1e6f)return false;
    const float values[]={p.fov,p.aspect,p.far_distance,p.projection_shift,p.plane.x,p.plane.y,p.plane.z,p.plane.w,p.rect.x,p.rect.y,p.rect.z,p.rect.w};
    for(float value:values)if(!std::isfinite(value))return false;
    if(!p.relative)
    {
        Fvector cross;cross.crossproduct(p.direction,p.up);
        if(cross.square_magnitude()<EPS_S)return false;
    }
    if(p.clip && p.plane.x*p.plane.x+p.plane.y*p.plane.y+p.plane.z*p.plane.z<EPS_S)return false;
    if(fabsf(p.projection_shift)>2.f||(p.fov>0&&p.fov<1))return false;
    if(p.transform_camera)
    {
        Fvector cross;cross.crossproduct(p.camera_transform.i,p.camera_transform.j);
        float determinant=cross.dotproduct(p.camera_transform.k);
        if(!std::isfinite(determinant)||fabsf(determinant)<EPS_S)return false;
        for(u32 r=0;r<4;++r)for(u32 c=0;c<4;++c)if(!std::isfinite(p.camera_transform.m[r][c]))return false;
        if(fabsf(p.camera_transform._14)>EPS||fabsf(p.camera_transform._24)>EPS||fabsf(p.camera_transform._34)>EPS||fabsf(p.camera_transform._44-1)>EPS)return false;
    }
    if(p.primary&&(p.output!=1||p.interval!=1))return false;
    if(p.fov<0||p.fov>150||p.aspect<0||p.aspect>4||p.far_distance<0||p.rect.z<=0||p.rect.w<=0||(!p.relative&&(p.direction.square_magnitude()<EPS||p.up.square_magnitude()<EPS)))return false;
    slots[id].p=p;slots[id].touch=Device.dwTimeContinual;slots[id].active=true;return true;
}
void CRender::R1ViewClear(u32 id){if(!r1mv::busy&&id<r1mv::count)r1mv::slots[id]=r1mv::Slot{};}
bool CRender::R1ViewMesh(u32 id,const R1ViewGeometry& g)
{
    if(r1mv::busy||id>=r1mv::count||g.vertices.size()>256||g.vertices.size()%3)return false;
    for(const auto& v:g.vertices)
        if(v.material>2||!std::isfinite(v.position.x)||!std::isfinite(v.position.y)||!std::isfinite(v.position.z)||!std::isfinite(v.uv.x)||!std::isfinite(v.uv.y))return false;
    auto& s=r1mv::slots[id];
    for(u32 i=0;i<2;++i)
    {
        if(s.geometry.textures[i]!=g.textures[i])
        {
            s.materials[i].destroy();
            if(g.textures[i].size()){s.materials[i].create(g.textures[i].c_str());s.materials[i]->Load();}
        }
    }
    s.geometry=g;return true;
}
bool CRender::R1ViewGet(u32 id,R1ViewStatus& s){if(id>=r1mv::count)return false;s=r1mv::slots[id].status;return r1mv::slots[id].active;}
void CRender::R1ViewReset()
{
    if(r1mv::busy)return;
    for(auto& s:r1mv::slots)s=r1mv::Slot{};
    for(auto& p:r1mv::pools)p.reset();
    r1mv::gpu=r1mv::GPU{};r1mv::uiCanvas=r1mv::Slot{};r1mv::uiPending=false;
    r1mv::screenFrame=u32(-1);
}
void CRender::R1ViewCompose()
{
    using namespace r1mv;
    if(uiPending)
    {
        Target->u_setrt(Device.dwWidth,Device.dwHeight,HW.pBaseRT,nullptr,nullptr,HW.pBaseZB);
        uiPending=false;
        for(auto& slot:slots)if(live(slot)&&slot.p.output==1&&slot.p.hud&&slot.status.ready&&!slot.status.error)
            gpu.draw(uiCanvas,false,&slot.p.rect,true);
        return;
    }
    if(busy||!R1ViewsActive()||Device.dwPrecacheFrame||Device.m_SecondViewport.IsSVPFrame()||o.dx10_msaa||o.dx11_hdr10)return;
    if(screenFrame==Device.dwFrame)return;
    bool composed=false,replaced=false;
    for(auto& slot:slots)if(live(slot)&&slot.p.output==1&&slot.status.ready&&!slot.status.error)
    {gpu.draw(slot,false);composed=true;replaced|=slot.p.primary;}
    if(!composed)return;
    screenFrame=Device.dwFrame;
    if(!replaced)return;
    // A replaced main world (e.g. stereo) needs per-output HUD UI. Ordinary PiP
    // keeps the main UI target. UI update callbacks remain outside view passes.
    if(!uiCanvas.image||uiCanvas.status.width!=Device.dwWidth||uiCanvas.status.height!=Device.dwHeight)
    {
        uiCanvas.image.destroy();uiCanvas.image.create("$user$r1mv_ui",Device.dwWidth,Device.dwHeight,D3DFMT_A8R8G8B8);
        uiCanvas.status.width=Device.dwWidth;uiCanvas.status.height=Device.dwHeight;uiCanvas.status.ready=true;
    }
    Target->u_setrt(uiCanvas.image,nullptr,nullptr,nullptr);
    FLOAT transparent[4]={0,0,0,0};HW.pContext->ClearRenderTargetView(uiCanvas.image->pRT,transparent);
    uiPending=true;
}
bool CRender::R1ViewsActive()
{
    if(!g_pGameLevel || !g_pGamePersistent || g_pGamePersistent->m_pMainMenu->IsActive())return false;
    for(auto& s:r1mv::slots)if(r1mv::live(s))return true;
    return false;
}
void CRender::Render()
{
    using namespace r1mv;
    if(busy||!R1ViewsActive()||Device.dwPrecacheFrame||m_bFirstFrameAfterReset||Device.m_SecondViewport.IsSVPFrame()) {R1RenderWorld();return;}
    bool needed=false;for(const auto& s:slots)needed|=live(s);
    if(!needed){R1RenderWorld();return;}
    if(o.dx10_msaa||o.dx11_hdr10){for(auto& s:slots)if(live(s))s.status.error=-2;R1RenderWorld();return;}
    // Existing ordinary-frame jobs finish before any temporary camera/resource changes.
    Camera camera;CSector* sector=pLastSector;Fvector lastPosition=vLastCameraPos;
    CDetailManager::vis_list detailLists[3];
    if(Details){Details->MT_SYNC();for(u32 i=0;i<3;++i)detailLists[i]=Details->m_visibles[i];}
    bool primary=false;
    for(u32 id=0;id<count;++id)
    {
        Slot& slot=slots[id];
        if(!live(slot)||!visible(slot))continue;
        if(Device.dwFrame%slot.p.interval && slot.status.ready)continue;
        Pool* pool=getPool(slot.p.width,slot.p.height);
        if(!pool){slot.status.error=-3;continue;}
        if(!slot.image||slot.status.width!=slot.p.width||slot.status.height!=slot.p.height)
        {
            slot.image.destroy();string128 name;xr_sprintf(name,"$user$r1mv_%u",id);
            slot.image.create(name,slot.p.width,slot.p.height,D3DFMT_A8R8G8B8);
        }
        CTimer timer;timer.Start();busy=true;
        const u32 frame=Device.dwFrame,oldWidth=Device.dwWidth,oldHeight=Device.dwHeight;
        auto oldRT=HW.pBaseRT;auto oldZB=HW.pBaseZB;
        const auto oldFlags=psHUD_Flags;const auto oldDeviceFlags=psDeviceFlags;
        const auto oldTAA=ps_ssfx_taa;const auto oldAO=ps_ssfx_ao;const auto oldSSR=ps_ssfx_ssr;
        const auto oldSSS=ps_ssfx_sss_quality;const auto oldOptions=o;
        const auto oldRenderFlags=ps_r2_ls_flags;
        RCache.OnFrameEnd();
        camera.restore();slot.source=camera.full;camera.apply(slot.p);
        pool->swap();Device.dwWidth=pool->width;Device.dwHeight=pool->height;
        HW.pBaseRT=pool->color->pRT;HW.pBaseZB=pool->depth->pZRT;
        Device.r1AuxiliaryView=true;Device.r1ViewToken=id+1;Device.r1ActorBody=slot.p.actor_body;
        ps_ssfx_taa.x=0;ps_ssfx_ssr.z=0;
        ps_r2_ls_flags.set(R2FLAG_EXP_MT_CALC,FALSE);
        if(!slot.p.hud)psHUD_Flags.zero();
        if(slot.p.quality==0){psDeviceFlags.set(rsDetails,FALSE);o.ssfx_sss=FALSE;o.ssfx_ssr=FALSE;ps_ssfx_ao.y=0;}
        if(slot.p.quality==1){ps_ssfx_sss_quality.x=_min(ps_ssfx_sss_quality.x,6.f);ps_ssfx_sss_quality.y=_min(ps_ssfx_sss_quality.y,2.f);}
        pLastSector=(CSector*)detectSector(Device.vCameraPosition);if(!pLastSector)pLastSector=sector;
        // Rebuild reduced-view LOD and visible geometry using the actual camera.
        Target->R1SetSize(pool->width,pool->height);Target->R1BeginPass();Calculate();
        if(Details)for(u32 i=0;i<3;++i)Details->m_visibles[i]=detailLists[i];
        RCache.set_RT(nullptr,0);RCache.set_RT(nullptr,1);RCache.set_RT(nullptr,2);RCache.set_RT(nullptr,3);RCache.set_ZB(nullptr);
        R1RenderWorld();
        if(frame!=Device.dwFrame)slot.status.error=-4;
        else
        {
            HW.pContext->CopyResource(slot.image->pSurface,pool->color->pSurface);
            slot.status.ready=true;slot.status.error=0;slot.status.renders++;slot.status.frame=frame;
            slot.status.width=pool->width;slot.status.height=pool->height;
            slot.status.render_ms=timer.GetElapsed_ms();slot.status.total_ms+=slot.status.render_ms;
        }
        Device.r1AuxiliaryView=false;Device.r1ViewToken=0;Device.r1ActorBody=false;
        HW.pBaseRT=oldRT;HW.pBaseZB=oldZB;Device.dwWidth=oldWidth;Device.dwHeight=oldHeight;
        pool->swap();RCache.OnFrameEnd();camera.restore();o=oldOptions;psHUD_Flags=oldFlags;psDeviceFlags=oldDeviceFlags;
        ps_ssfx_taa=oldTAA;ps_ssfx_ao=oldAO;ps_ssfx_ssr=oldSSR;ps_ssfx_sss_quality=oldSSS;
        ps_r2_ls_flags=oldRenderFlags;
        pLastSector=sector;vLastCameraPos=lastPosition;busy=false;
        primary|=slot.p.primary&&slot.status.ready&&!slot.status.error;
    }
    camera.restore();Target->R1SetSize(Device.dwWidth,Device.dwHeight);Target->R1BeginPass();Calculate();
    if(Details)
    {
        if(!primary)for(u32 i=0;i<3;++i)Details->m_visibles[i]=detailLists[i];
        else
        {
            // UpdateVisibleM appends; the ordinary world pass normally consumes
            // these lists. A replaced main view must consume them here as well.
            for(auto& type:Details->m_visibles)for(auto& model:type)model.clear_not_free();
            // Keep the detail cache schedulable when auxiliary quality disables
            // drawing grass. This is a render marker, never a simulation tick.
            Details->m_frame_rendered=Device.dwFrame;
        }
    }
    if(!primary){ViewBase.CreateFromMatrix(Device.mFullTransform,FRUSTUM_P_LRTB+FRUSTUM_P_FAR);HOM.Enable();HOM.Render(ViewBase);R1RenderWorld();}
    else
    {
        Target->u_setrt(Device.dwWidth,Device.dwHeight,HW.pBaseRT,nullptr,nullptr,HW.pBaseZB);
        Fvector4 full;full.set(0,0,1,1);
        for(auto& s:slots)if(live(s)&&s.p.primary){gpu.draw(s,false,&full);break;}
    }
    for(auto& s:slots)if(live(s)&&s.p.output==2&&visible(s))gpu.draw(s,true);
}
