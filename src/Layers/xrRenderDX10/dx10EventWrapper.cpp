#include "stdafx.h"
#pragma hdrstop
#include "dx10EventWrapper.h"
#include "../xrRender/HW.h"

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