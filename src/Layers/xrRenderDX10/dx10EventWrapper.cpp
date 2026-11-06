#include "stdafx.h"
#pragma hdrstop
#include "dx10EventWrapper.h"
#include "dx10GpuTimeline.h"
#include "../xrRender/HW.h"

dxPixEventWrapper::dxPixEventWrapper(LPCWSTR wszName) :
    gpu_scope(ps_r__gpu_timeline ? gpu_timeline::scope_begin(wszName) : gpu_timeline::no_scope)
{
    if (HW.pAnnotation)
        HW.pAnnotation->BeginEvent(wszName);
}

dxPixEventWrapper::~dxPixEventWrapper()
{
    if (HW.pAnnotation)
        HW.pAnnotation->EndEvent();

    if (gpu_scope != gpu_timeline::no_scope)
        gpu_timeline::scope_end(gpu_scope);
} 