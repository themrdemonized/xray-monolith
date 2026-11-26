#include "stdafx.h"
#include "dxImGuiRender.h"

#include <imgui.h>

#if defined(USE_DX11)
#include <backends/imgui_impl_dx11.h>
#elif defined(USE_DX10)
#include <backends/imgui_impl_dx10.h>
#else
#include <backends/imgui_impl_dx9.h>
#endif

void dxImGuiRender::Copy(IImGuiRender& _in)
{
    *this = *fast_dynamic_cast<dxImGuiRender*>(&_in);
}

void dxImGuiRender::SetState(ImDrawData* data)
{
#if defined(USE_DX11) || defined(USE_DX10)
    D3D_VIEWPORT VP = { 0, 0, data->DisplaySize.x, data->DisplaySize.y, 0, 1.f };
#if defined(USE_DX11)
    HW.pContext->RSSetViewports(1, &VP);
#else
    HW.pDevice->RSSetViewports(1, &VP);
#endif
#else
    D3DVIEWPORT9 VP = { 0, 0, data->DisplaySize.x, data->DisplaySize.y, 0, 1.f };
    HW.pDevice->SetViewport(&VP);
#endif

    // Setup shader and vertex buffers
    /*unsigned int stride = sizeof(ImDrawVert);
    unsigned int offset = 0;
    ctx->IASetInputLayout(bd->pInputLayout);
    ctx->IASetVertexBuffers(0, 1, &bd->pVB, &stride, &offset);
    ctx->IASetIndexBuffer(bd->pIB, sizeof(ImDrawIdx) == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(bd->pVertexShader, nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, &bd->pVertexConstantBuffer);
    ctx->PSSetShader(bd->pPixelShader, nullptr, 0);
    ctx->PSSetSamplers(0, 1, &bd->pFontSampler);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ctx->HSSetShader(nullptr, nullptr, 0); // In theory we should backup and restore this as well.. very infrequently used..
    ctx->DSSetShader(nullptr, nullptr, 0); // In theory we should backup and restore this as well.. very infrequently used..
    ctx->CSSetShader(nullptr, nullptr, 0); // In theory we should backup and restore this as well.. very infrequently used..

    // Setup blend state
    const float blend_factor[4] = { 0.f, 0.f, 0.f, 0.f };
    ctx->OMSetBlendState(bd->pBlendState, blend_factor, 0xffffffff);
    ctx->OMSetDepthStencilState(bd->pDepthStencilState, 0);
    ctx->RSSetState(bd->pRasterizerState);*/
}

void dxImGuiRender::Frame()
{
#if defined(USE_DX11)
    ImGui_ImplDX11_NewFrame();
#elif defined(USE_DX10)
    ImGui_ImplDX10_NewFrame();
#else
    ImGui_ImplDX9_NewFrame();
#endif
}

void dxImGuiRender::Render(ImDrawData* data)
{
#if defined(USE_DX11)
    ImGui_ImplDX11_RenderDrawData(data);
#elif defined(USE_DX10)
    ImGui_ImplDX10_RenderDrawData(data);
#else
    ImGui_ImplDX9_RenderDrawData(data);
#endif
}

void dxImGuiRender::OnDeviceCreate(ImGuiContext* context)
{
    ImGui::SetAllocatorFunctions(
        [](size_t size, void* /*user_data*/)
        {
            return xr_malloc(size);
        },
        [](void* ptr, void* /*user_data*/)
        {
            xr_free(ptr);
        }
    );
    ImGui::SetCurrentContext(context);

#if defined(USE_DX11)
    ImGui_ImplDX11_Init(HW.pDevice, HW.pContext);
#elif defined(USE_DX10)
    ImGui_ImplDX10_Init(HW.pDevice);
#else
    ImGui_ImplDX9_Init(HW.pDevice);
#endif
}
void dxImGuiRender::OnDeviceDestroy()
{
#if defined(USE_DX11)
    ImGui_ImplDX11_Shutdown();
#elif defined(USE_DX10)
    ImGui_ImplDX10_Shutdown();
#else
    ImGui_ImplDX9_Shutdown();
#endif
}

void dxImGuiRender::OnDeviceResetBegin()
{
#if defined(USE_DX11)
    ImGui_ImplDX11_InvalidateDeviceObjects();
#elif defined(USE_DX10)
    ImGui_ImplDX10_InvalidateDeviceObjects();
#else
    ImGui_ImplDX9_InvalidateDeviceObjects();
#endif
}

void dxImGuiRender::OnDeviceResetEnd()
{
#if defined(USE_DX11)
    ImGui_ImplDX11_CreateDeviceObjects();
#elif defined(USE_DX10)
    ImGui_ImplDX10_CreateDeviceObjects();
#else
    ImGui_ImplDX9_CreateDeviceObjects();
#endif
}