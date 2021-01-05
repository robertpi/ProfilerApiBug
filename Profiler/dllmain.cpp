// Copyright (c) .NET Foundation and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <iostream>
#include "ClassFactory.h"
#include "util.h"

const IID IID_IUnknown      = { 0x00000000, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

const IID IID_IClassFactory = { 0x00000001, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

BOOL STDMETHODCALLTYPE DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    return TRUE;
}

extern "C" HRESULT STDMETHODCALLTYPE DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
    //exit(1);

    std::wcout << "DllGetClassObject: starting ..." << std::endl;;

    // {88E5B029-D6B4-4709-B445-03E9BDAB2FA2}
    const GUID CLSID_CorProfiler = { 0x88e5b029, 0xd6b4, 0x4709, { 0xb4, 0x45, 0x3, 0xe9, 0xbd, 0xab, 0x2f, 0xa2 } };
    
    // {af0d821e-299b-5307-a3d8-b283c03916dd}
    const GUID CLSID_CorProfiler2 = { 0xaf0d821e, 0x299b, 0x5307, { 0xa3, 0xd8, 0xb2, 0x83, 0xc0, 0x39, 0x16, 0xdd } };

    if (ppv == nullptr || !(rclsid == CLSID_CorProfiler || rclsid == CLSID_CorProfiler2))
    {
        return E_FAIL;
    }

    auto factory = new ClassFactory;
    if (factory == nullptr)
    {
        return E_FAIL;
    }

    trace::SetClrProfilerFlag(rclsid == CLSID_CorProfiler);

    return factory->QueryInterface(riid, ppv);
}

extern "C" HRESULT STDMETHODCALLTYPE DllCanUnloadNow()
{
    return S_OK;
}
