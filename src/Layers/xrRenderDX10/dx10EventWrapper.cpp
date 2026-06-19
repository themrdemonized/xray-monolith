#include "stdafx.h"
#pragma hdrstop
#include "dx10EventWrapper.h"
#include "../xrRender/HW.h"
#include "../xrRender/xrRender_console.h" // r__gpu_markers

#include <stdarg.h>

dxPixEventWrapper::dxPixEventWrapper(LPCWSTR wszName)
{
    if (HW.pAnnotation)
        HW.pAnnotation->BeginEvent(wszName);
}

dxPixEventWrapper::~dxPixEventWrapper()
{
    if (HW.pAnnotation)
        HW.pAnnotation->EndEvent();
}

// per-batch marker, skipped when r__gpu_markers is off
dxPixEventScope::dxPixEventScope(const char* fmt, ...) : active(false)
{
    if (!r__gpu_markers || !HW.pAnnotation)
        return;

    char nbuf[256];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(nbuf, sizeof(nbuf), _TRUNCATE, fmt, ap);
    va_end(ap);

    wchar_t wbuf[256];
    ::MultiByteToWideChar(CP_ACP, 0, nbuf, -1, wbuf, (int)(sizeof(wbuf) / sizeof(wbuf[0])));

    HW.pAnnotation->BeginEvent(wbuf);
    active = true;
}

dxPixEventScope::~dxPixEventScope()
{
    if (active)
        HW.pAnnotation->EndEvent();
}

// WKPDID_D3DDebugObjectName, defined locally to avoid a dxguid.lib dependency
static const GUID kD3DDebugObjectName =
    { 0x429b8c22, 0x9188, 0x4b0c, { 0x87, 0x42, 0xac, 0xb0, 0xbf, 0x85, 0xc2, 0x00 } };

void dx10_set_debug_name(ID3D11DeviceChild* res, const char* name)
{
    if (!r__gpu_markers || !res || !name || !name[0])
        return;

    res->SetPrivateData(kD3DDebugObjectName, (UINT)xr_strlen(name), name);
}
