#pragma once

#define PIX_EVENT(Name) dxPixEventWrapper pixEvent##Name(L#Name)

class dxPixEventWrapper
{
    u32 gpu_scope;

public:
    dxPixEventWrapper(LPCWSTR wszName);
    ~dxPixEventWrapper();
}; 