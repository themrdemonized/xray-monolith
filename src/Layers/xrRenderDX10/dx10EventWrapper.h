#pragma once

// Each scope is also a profiler zone of the same name
#define PIX_EVENT(Name) PROF_EVENT(#Name, PROF_CAT(Rendering)); dxPixEventWrapper pixEvent##Name(L#Name)

class dxPixEventWrapper
{
    u32 gpu_scope;

public:
    dxPixEventWrapper(LPCWSTR wszName);
    ~dxPixEventWrapper();
}; 