#pragma once

// GPU debug-event markers over ID3DUserDefinedAnnotation (HW.pAnnotation), no-op when no debugger attached
//   PIX_EVENT(Name)      always-on phase marker, Name is a bare identifier
//   PIX_EVENT_F(fmt,...) per-batch marker with a printf label, uniqued by __LINE__, gated on r__gpu_markers

#define PIX_EVENT(Name) dxPixEventWrapper pixEvent##Name(L#Name)

class dxPixEventWrapper
{
public:
    dxPixEventWrapper(LPCWSTR wszName);
    ~dxPixEventWrapper();
};

// uniqued by __LINE__ so several can share one block
#define PIX_EVENT_F_CAT_(a, b) a##b
#define PIX_EVENT_F_CAT(a, b) PIX_EVENT_F_CAT_(a, b)
#define PIX_EVENT_F(...) dxPixEventScope PIX_EVENT_F_CAT(pixScope_, __LINE__)(__VA_ARGS__)

class dxPixEventScope
{
    bool active;

public:
    explicit dxPixEventScope(const char* fmt, ...);
    ~dxPixEventScope();
};

// tag a D3D11 resource/view with a debug name (WKPDID_D3DDebugObjectName) so RenderDoc shows readable
// names instead of "Render Target NNNN", gated on r__gpu_markers, null-safe
struct ID3D11DeviceChild;
void dx10_set_debug_name(ID3D11DeviceChild* res, const char* name);
