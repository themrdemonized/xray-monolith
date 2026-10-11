#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstring>
using Microsoft::WRL::ComPtr;
struct R1GPU {
    ComPtr<ID3D11Texture2D> left,right;
    ComPtr<ID3D11ShaderResourceView> left_view,right_view;
    ComPtr<ID3D11DeviceContext> commands;
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11SamplerState> sampler;ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11DepthStencilState> depth;ComPtr<ID3D11BlendState> blend;
    UINT width=0,height=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    bool setup(ID3D11Device* d,ID3D11Texture2D* back){
        D3D11_TEXTURE2D_DESC desc{};back->GetDesc(&desc);
        if(desc.SampleDesc.Count!=1||desc.Width%2||desc.Width<2||desc.Height==0)return false;
        if(!(vs&&ps&&commands&&sampler&&raster&&depth&&blend)){
            vs.Reset();ps.Reset();commands.Reset();sampler.Reset();raster.Reset();depth.Reset();blend.Reset();
            const char* source=R"(
Texture2D leftTex:register(t0);Texture2D rightTex:register(t1);SamplerState smp:register(s0);
struct V {float4 p:SV_Position;float2 uv:TEXCOORD;};
V VS(uint id:SV_VertexID){V o;o.uv=float2((id<<1)&2,id&2);o.p=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o;}
float4 PS(V p):SV_Target {float2 uv=float2(frac(p.uv.x*2),p.uv.y);return p.uv.x<.5?leftTex.Sample(smp,uv):rightTex.Sample(smp,uv);}
)";
            ComPtr<ID3DBlob> a,b,error;
            if(FAILED(D3DCompile(source,strlen(source),"R1_SBS",nullptr,nullptr,"VS","vs_5_0",0,0,&a,&error))||
               FAILED(D3DCompile(source,strlen(source),"R1_SBS",nullptr,nullptr,"PS","ps_5_0",0,0,&b,&error)))return false;
            if(FAILED(d->CreateVertexShader(a->GetBufferPointer(),a->GetBufferSize(),nullptr,&vs))||
               FAILED(d->CreatePixelShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&ps))||
               FAILED(d->CreateDeferredContext(0,&commands)))return false;
            D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
            D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
            D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=FALSE;dd.StencilEnable=FALSE;
            D3D11_BLEND_DESC bd{};bd.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
            if(FAILED(d->CreateSamplerState(&sd,&sampler))||FAILED(d->CreateRasterizerState(&rd,&raster))||
               FAILED(d->CreateDepthStencilState(&dd,&depth))||FAILED(d->CreateBlendState(&bd,&blend)))return false;
        }
        if(left&&right&&left_view&&right_view&&width==desc.Width&&height==desc.Height&&format==desc.Format)return true;
        left.Reset();right.Reset();left_view.Reset();right_view.Reset();width=desc.Width;height=desc.Height;format=desc.Format;
        desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.CPUAccessFlags=0;desc.MiscFlags=0;
        return SUCCEEDED(d->CreateTexture2D(&desc,nullptr,&left))&&SUCCEEDED(d->CreateTexture2D(&desc,nullptr,&right))&&
               SUCCEEDED(d->CreateShaderResourceView(left.Get(),nullptr,&left_view))&&SUCCEEDED(d->CreateShaderResourceView(right.Get(),nullptr,&right_view));
    }
    bool draw(ID3D11DeviceContext* immediate,ID3D11RenderTargetView* output){
        auto c=commands.Get();c->ClearState();c->OMSetRenderTargets(1,&output,nullptr);c->OMSetDepthStencilState(depth.Get(),0);
        const float factors[4]{};c->OMSetBlendState(blend.Get(),factors,~0u);
        D3D11_VIEWPORT v{0,0,float(width),float(height),0,1};c->RSSetViewports(1,&v);c->RSSetState(raster.Get());
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);c->VSSetShader(vs.Get(),nullptr,0);c->PSSetShader(ps.Get(),nullptr,0);
        ID3D11ShaderResourceView* views[]={left_view.Get(),right_view.Get()};c->PSSetShaderResources(0,2,views);
        auto s=sampler.Get();c->PSSetSamplers(0,1,&s);c->Draw(3,0);
        ComPtr<ID3D11CommandList> list;if(FAILED(c->FinishCommandList(FALSE,&list)))return false;
        immediate->ExecuteCommandList(list.Get(),TRUE);return true;
    }
};
