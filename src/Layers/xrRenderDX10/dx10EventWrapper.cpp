#include "stdafx.h"
#pragma hdrstop
#include "dx10EventWrapper.h"
#include "../xrRender/HW.h"
#include "../../xrEngine/renderdoc_integration.h"

namespace
{
    typedef int(WINAPI* pfn_perf_begin_event)(DWORD color, LPCWSTR name);
    typedef int(WINAPI* pfn_perf_end_event)(void);
    typedef DWORD(WINAPI* pfn_perf_get_status)(void);

    struct dx10_perf_markers
    {
        pfn_perf_begin_event begin;
        pfn_perf_end_event end;
        pfn_perf_get_status status;

        dx10_perf_markers() : begin(nullptr), end(nullptr), status(nullptr)
        {
            // RenderDoc hooks d3d9 by module name so the system copy is the right one
            const HMODULE d3d9 = LoadLibraryExW(L"d3d9.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!d3d9)
                return;

            begin = reinterpret_cast<pfn_perf_begin_event>(GetProcAddress(d3d9, "D3DPERF_BeginEvent"));
            end = reinterpret_cast<pfn_perf_end_event>(GetProcAddress(d3d9, "D3DPERF_EndEvent"));
            status = reinterpret_cast<pfn_perf_get_status>(GetProcAddress(d3d9, "D3DPERF_GetStatus"));
        }

        bool hooked() const { return begin && end && status && status() != 0; }
    };

    const dx10_perf_markers& dx10_perf()
    {
        static const dx10_perf_markers markers;
        return markers;
    }

    enum dx10_marker_sink
    {
        dx10_sink_none,
        dx10_sink_annotation,
        dx10_sink_perf,
    };

    dx10_marker_sink dx10_sink(ID3DUserDefinedAnnotation* annotation)
    {
        static thread_local ID3DUserDefinedAnnotation* cached_annotation = nullptr;
        static thread_local u32 cached_frame = u32(-1);
        static thread_local dx10_marker_sink cached_sink = dx10_sink_none;

        if (cached_annotation != annotation || cached_frame != Device.dwFrame)
        {
            cached_annotation = annotation;
            cached_frame = Device.dwFrame;

            // RenderDoc leaves the annotation status clear until a capture starts
            const bool listening = renderdoc_api_live() || annotation->GetStatus() != FALSE;

            cached_sink = dx10_sink_none;
            if (listening)
                cached_sink = dx10_perf().hooked() ? dx10_sink_perf : dx10_sink_annotation;
        }

        return cached_sink;
    }
}

dxPixEventWrapper::dxPixEventWrapper(LPCWSTR wszName, u32 color) : annotation(nullptr), perf_event(false)
{
    ID3DUserDefinedAnnotation* const listener = HW.pAnnotation;
    if (!listener)
        return;

    const dx10_marker_sink sink = dx10_sink(listener);
    if (sink == dx10_sink_none)
        return;

    if (color && sink == dx10_sink_perf && dx10_perf().begin(color, wszName) >= 0)
    {
        perf_event = true;
        return;
    }

    if (listener->BeginEvent(wszName) >= 0)
        annotation = listener;
}

dxPixEventWrapper::~dxPixEventWrapper()
{
    if (perf_event)
        dx10_perf().end();
    else if (annotation)
        annotation->EndEvent();
}
