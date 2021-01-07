// Copyright (c) .NET Foundation and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <iostream>
#include <fcntl.h>
#include "Profiler.h"
#include "CComPtr.h"
#include "corhlpr.h"
#include "macros.h"
#include "clr_helpers.h"
#include "il_rewriter.h"
#include "il_rewriter_wrapper.h"
#include <string>
#include <vector>
#include <cassert>

namespace trace {
    BOOL debug = false;

    Profiler::Profiler() : refCount(0), corProfilerInfo(nullptr)
    {
        if (debug) std::wcout << "Profiler()" << std::endl;;
        GetSingletonish() = this;
    }

    Profiler::~Profiler()
    {
        if (this->corProfilerInfo != nullptr)
        {
            this->corProfilerInfo->Release();
            this->corProfilerInfo = nullptr;
        }
    }

    HRESULT STDMETHODCALLTYPE Profiler::Initialize(IUnknown *pIProfilerInfoUnk)
    {
        //  this project agent support net461+ , if support net45 use IProfilerInfo4
        const HRESULT queryHR = pIProfilerInfoUnk->QueryInterface(__uuidof(ICorProfilerInfo8), reinterpret_cast<void **>(&this->corProfilerInfo));

        if (FAILED(queryHR))
        {
            return E_FAIL;
        }

        const DWORD COR_PRF_ENABLE_REJIT = 0x00040000;

        const DWORD eventMask = COR_PRF_MONITOR_JIT_COMPILATION |
            COR_PRF_DISABLE_TRANSPARENCY_CHECKS_UNDER_FULL_TRUST | /* helps the case where this profiler is used on Full CLR */
            COR_PRF_DISABLE_INLINING |
            COR_PRF_MONITOR_MODULE_LOADS |
            COR_PRF_DISABLE_ALL_NGEN_IMAGES |
            COR_PRF_ENABLE_REJIT;

        this->corProfilerInfo->SetEventMask(eventMask);

        if (debug) std::wcout << "Profiler Initialize Success\n";

        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::Shutdown()
    {
        if (debug) std::wcout << "Profiler Shutdown\n";

        if (this->corProfilerInfo != nullptr)
        {
            this->corProfilerInfo->Release();
            this->corProfilerInfo = nullptr;
        }

        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::AppDomainCreationStarted(AppDomainID appDomainId)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::AppDomainCreationFinished(AppDomainID appDomainId, HRESULT hrStatus)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::AppDomainShutdownStarted(AppDomainID appDomainId)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::AppDomainShutdownFinished(AppDomainID appDomainId, HRESULT hrStatus)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::AssemblyLoadStarted(AssemblyID assemblyId)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::AssemblyLoadFinished(AssemblyID assemblyId, HRESULT hrStatus)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::AssemblyUnloadStarted(AssemblyID assemblyId)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::AssemblyUnloadFinished(AssemblyID assemblyId, HRESULT hrStatus)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ModuleLoadStarted(ModuleID moduleId)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ModuleLoadFinished(ModuleID moduleId, HRESULT hrStatus) 
    {
        // used to store info about the modules that will be useful later for rewriting

        auto module_info = GetModuleInfo(this->corProfilerInfo, moduleId);
        if (!module_info.IsValid() || module_info.IsWindowsRuntime()) {
            return S_OK;
        }

        if (module_info.assembly.name == "dotnet"_W ||
            module_info.assembly.name == "MSBuild"_W)
        {
            return S_OK;
        }

        const auto entryPointToken = module_info.GetEntryPointToken();
        ModuleMetaInfo* module_metadata = new ModuleMetaInfo(entryPointToken, module_info.assembly.name);
        {
            std::lock_guard<std::mutex> guard(mapLock);
            moduleMetaInfoMap[moduleId] = module_metadata;
        }

        // only log the load of the module with an entry point, otherwise we'll spam the logs
        if (entryPointToken != mdTokenNil)
        {
            if (debug) std::wcout << "Assembly: " << module_info.assembly.name << ", EntryPointToken: " << entryPointToken << "\n";
        }

        if (module_info.assembly.name == "mscorlib"_W || module_info.assembly.name == "System.Private.CoreLib"_W) {
                                  
            if(!corAssemblyProperty.szName.empty()) {
                return S_OK;
            }

            CComPtr<IUnknown> metadata_interfaces;
            auto hr = corProfilerInfo->GetModuleMetaData(moduleId, ofRead | ofWrite,
                IID_IMetaDataImport2,
                metadata_interfaces.GetAddressOf());
            RETURN_OK_IF_FAILED(hr);

            auto pAssemblyImport = metadata_interfaces.As<IMetaDataAssemblyImport>(
                IID_IMetaDataAssemblyImport);
            if (pAssemblyImport.IsNull()) {
                return S_OK;
            }

            mdAssembly assembly;
            hr = pAssemblyImport->GetAssemblyFromScope(&assembly);
            RETURN_OK_IF_FAILED(hr);

            hr = pAssemblyImport->GetAssemblyProps(
                assembly,
                &corAssemblyProperty.ppbPublicKey,
                &corAssemblyProperty.pcbPublicKey,
                &corAssemblyProperty.pulHashAlgId,
                NULL,
                0,
                NULL,
                &corAssemblyProperty.pMetaData,
                &corAssemblyProperty.assemblyFlags);
            RETURN_OK_IF_FAILED(hr);

            corAssemblyProperty.szName = module_info.assembly.name;

            return S_OK;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ModuleUnloadStarted(ModuleID moduleId)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ModuleUnloadFinished(ModuleID moduleId, HRESULT hrStatus)
    {
        // remove info about the module on unload

        if (debug) std::wcout << "Profiler::ModuleUnloadFinished, ModuleID: " << moduleId << "\n";
        {
            std::lock_guard<std::mutex> guard(mapLock);
            if (moduleMetaInfoMap.count(moduleId) > 0) {
                const auto moduleMetaInfo = moduleMetaInfoMap[moduleId];
                delete moduleMetaInfo;
                moduleMetaInfoMap.erase(moduleId);
            }
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ModuleAttachedToAssembly(ModuleID moduleId, AssemblyID AssemblyId)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ClassLoadStarted(ClassID classId)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ClassLoadFinished(ClassID classId, HRESULT hrStatus)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ClassUnloadStarted(ClassID classId)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ClassUnloadFinished(ClassID classId, HRESULT hrStatus)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::FunctionUnloadStarted(FunctionID functionId)
    {
        return S_OK;
    }

    HRESULT Profiler::RewriteMethod(WSTRING targetFunction, FunctionID functionId, ICorProfilerFunctionControl* pICorProfilerFunctionControl)
    {
        // get the method's module and function token
        mdToken function_token = mdTokenNil;
        ModuleID moduleId;
        auto hr = corProfilerInfo->GetFunctionInfo(functionId, NULL, &moduleId, &function_token);
        RETURN_OK_IF_FAILED(hr);

        ModuleMetaInfo* moduleMetaInfo = nullptr;
        {
            std::lock_guard<std::mutex> guard(mapLock);
            if (moduleMetaInfoMap.count(moduleId) > 0) {
                moduleMetaInfo = moduleMetaInfoMap[moduleId];
            }
        }
        if (moduleMetaInfo == nullptr) {
            return S_OK;
        }

        InnerRewrite(targetFunction, moduleId, function_token, pICorProfilerFunctionControl);
        InnerRewrite(targetFunction, moduleId, function_token, pICorProfilerFunctionControl);

        // check if method has already been written
        bool isiLRewrote = false;
        {
            std::lock_guard<std::mutex> guard(mapLock);
            if (iLRewriteMap.count(function_token) > 0) {
                isiLRewrote = true;
            }
        }
        if (isiLRewrote) {
            return S_OK;
        }


        // exit rewrite lock
        {
            std::lock_guard<std::mutex> guard(mapLock);
            iLRewriteMap[function_token] = true;
        }

        return S_OK;
    }

    HRESULT Profiler::InnerRewrite(WSTRING targetFunction, ModuleID moduleId, mdToken function_token, ICorProfilerFunctionControl* pICorProfilerFunctionControl)
    {
        // extract some COM interfaces needed for querying the meta and rewriting the IL
        CComPtr<IUnknown> metadata_interfaces;
        auto hr = corProfilerInfo->GetModuleMetaData(moduleId, ofRead | ofWrite,
            IID_IMetaDataImport2,
            metadata_interfaces.GetAddressOf());
        RETURN_OK_IF_FAILED(hr);

        auto pImport = metadata_interfaces.As<IMetaDataImport2>(IID_IMetaDataImport);
        auto pEmit = metadata_interfaces.As<IMetaDataEmit2>(IID_IMetaDataEmit);
        if (pEmit.IsNull() || pImport.IsNull()) {
            return S_OK;
        }

        // find the meta data about the method being JIT compiled
        mdModule module;
        hr = pImport->GetModuleFromScope(&module);
        RETURN_OK_IF_FAILED(hr);

        auto functionInfo = GetFunctionInfo(pImport, function_token);
        if (!functionInfo.IsValid()) {
            return S_OK;
        }

        FunctionMetaInfo* functionMetadata = new FunctionMetaInfo(moduleId, function_token);
        {
            std::lock_guard<std::mutex> guard(mapLock);
            functionNameMetaInfoMap[functionInfo.type.name + "."_W + functionInfo.name] = functionMetadata;
        }


        // some generic test on the signature and calling convertion
        hr = functionInfo.signature.TryParse();
        RETURN_OK_IF_FAILED(hr);


        if (targetFunction != functionInfo.name)
        {
            return S_OK;
        }

        if (debug) std::wcout << "Starting rewrite: " << functionInfo.type.name << "." << functionInfo.name << std::endl;


        //return ref not support
        unsigned elementType;
        auto retTypeFlags = functionInfo.signature.GetRet().GetTypeFlags(elementType);
        if (retTypeFlags & TypeFlagByRef) {
            return S_OK;
        }


        // get a refernce to another COM interface to find an assemble that contains a type from our target functions signature
        auto importMetaDataAssembly = metadata_interfaces.As<IMetaDataAssemblyImport>(IID_IMetaDataAssemblyImport);
        if (importMetaDataAssembly.IsNull())
        {
            return S_OK;
        }

        // get a reference to the middleware / profiler assembly
        mdAssemblyRef consoleAssemblyRef = FindAssemblyRef(importMetaDataAssembly, ConsoleAssemblyName);

        if (consoleAssemblyRef == mdAssemblyRefNil) {
            return S_OK;
        }

        mdString testMessageToken;
        auto testMessage = "Hello from "_W + functionInfo.name + "!"_W;
        hr = pEmit->DefineUserString(testMessage.data(), (ULONG)testMessage.length(), &testMessageToken);

        // get a reference to the middleware type
        mdTypeRef consoleTypeRef;
        hr = pEmit->DefineTypeRefByName(
            consoleAssemblyRef,
            ConsoleTypeName.data(),
            &consoleTypeRef);
        RETURN_OK_IF_FAILED(hr);

        // build a structure representing the signature of the middleware function to be called
        auto* consoleWriteLineSig = new COR_SIGNATURE[4];
        unsigned offset = 0;
        consoleWriteLineSig[offset++] = IMAGE_CEE_CS_CALLCONV_DEFAULT;
        consoleWriteLineSig[offset++] = 0x01; // number parameters
        consoleWriteLineSig[offset++] = ELEMENT_TYPE_VOID; // return type
        consoleWriteLineSig[offset++] = ELEMENT_TYPE_STRING; // parameter type

        // reference to the signature of the middleware
        mdMemberRef consoleWriteLineMemberRef;
        hr = pEmit->DefineMemberRef(
            consoleTypeRef,
            ConsoleWriteLineMethodName.data(),
            consoleWriteLineSig,
            sizeof(consoleWriteLineSig),
            &consoleWriteLineMemberRef);
        RETURN_OK_IF_FAILED(hr);

        // start the IL rewriting
        ILRewriter rewriter(corProfilerInfo, pICorProfilerFunctionControl, moduleId, function_token);
        RETURN_OK_IF_FAILED(rewriter.Import());

        // find position to start rewriting
        auto pReWriter = &rewriter;
        ILRewriterWrapper reWriterWrapper(pReWriter);
        ILInstr* pFirstOriginalInstr = pReWriter->GetILList()->m_pNext;
        reWriterWrapper.SetILPosition(pFirstOriginalInstr);

        // load the functions first parameter on to the stack (zero is this pointer)
        reWriterWrapper.LoadStr(testMessageToken);

        // make the call to target middleware setup function / method
        reWriterWrapper.CallMember0(consoleWriteLineMemberRef, false);

        // finish rewriting
        hr = rewriter.Export();
        RETURN_OK_IF_FAILED(hr);

        if (debug) std::wcout << "Finished rewrite: " << functionInfo.type.name << "." << functionInfo.name << "\n";

        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::JITCompilationStarted(FunctionID functionId, BOOL fIsSafeToBlock)
    {
        return RewriteMethod("JitRewriteTarget"_W, functionId, NULL);
    }

    HRESULT STDMETHODCALLTYPE Profiler::JITCompilationFinished(FunctionID functionId, HRESULT hrStatus, BOOL fIsSafeToBlock)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::JITCachedFunctionSearchStarted(FunctionID functionId, BOOL *pbUseCachedFunction)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::JITCachedFunctionSearchFinished(FunctionID functionId, COR_PRF_JIT_CACHE result)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::JITFunctionPitched(FunctionID functionId)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::JITInlining(FunctionID callerId, FunctionID calleeId, BOOL *pfShouldInline)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ThreadCreated(ThreadID threadId)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ThreadDestroyed(ThreadID threadId)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ThreadAssignedToOSThread(ThreadID managedThreadId, DWORD osThreadId)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::RemotingClientInvocationStarted()
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::RemotingClientSendingMessage(GUID *pCookie, BOOL fIsAsync)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::RemotingClientReceivingReply(GUID *pCookie, BOOL fIsAsync)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::RemotingClientInvocationFinished()
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::RemotingServerReceivingMessage(GUID *pCookie, BOOL fIsAsync)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::RemotingServerInvocationStarted()
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::RemotingServerInvocationReturned()
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::RemotingServerSendingReply(GUID *pCookie, BOOL fIsAsync)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::UnmanagedToManagedTransition(FunctionID functionId, COR_PRF_TRANSITION_REASON reason)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ManagedToUnmanagedTransition(FunctionID functionId, COR_PRF_TRANSITION_REASON reason)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::RuntimeSuspendStarted(COR_PRF_SUSPEND_REASON suspendReason)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::RuntimeSuspendFinished()
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::RuntimeSuspendAborted()
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::RuntimeResumeStarted()
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::RuntimeResumeFinished()
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::RuntimeThreadSuspended(ThreadID threadId)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::RuntimeThreadResumed(ThreadID threadId)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::MovedReferences(ULONG cMovedObjectIDRanges, ObjectID oldObjectIDRangeStart[], ObjectID newObjectIDRangeStart[], ULONG cObjectIDRangeLength[])
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ObjectAllocated(ObjectID objectId, ClassID classId)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ObjectsAllocatedByClass(ULONG cClassCount, ClassID classIds[], ULONG cObjects[])
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ObjectReferences(ObjectID objectId, ClassID classId, ULONG cObjectRefs, ObjectID objectRefIds[])
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::RootReferences(ULONG cRootRefs, ObjectID rootRefIds[])
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ExceptionThrown(ObjectID thrownObjectId)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ExceptionSearchFunctionEnter(FunctionID functionId)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ExceptionSearchFunctionLeave()
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ExceptionSearchFilterEnter(FunctionID functionId)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ExceptionSearchFilterLeave()
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ExceptionSearchCatcherFound(FunctionID functionId)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ExceptionOSHandlerEnter(UINT_PTR __unused)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ExceptionOSHandlerLeave(UINT_PTR __unused)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ExceptionUnwindFunctionEnter(FunctionID functionId)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ExceptionUnwindFunctionLeave()
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ExceptionUnwindFinallyEnter(FunctionID functionId)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ExceptionUnwindFinallyLeave()
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ExceptionCatcherEnter(FunctionID functionId, ObjectID objectId)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ExceptionCatcherLeave()
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::COMClassicVTableCreated(ClassID wrappedClassId, REFGUID implementedIID, void *pVTable, ULONG cSlots)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::COMClassicVTableDestroyed(ClassID wrappedClassId, REFGUID implementedIID, void *pVTable)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ExceptionCLRCatcherFound()
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ExceptionCLRCatcherExecute()
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ThreadNameChanged(ThreadID threadId, ULONG cchName, WCHAR name[])
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::GarbageCollectionStarted(int cGenerations, BOOL generationCollected[], COR_PRF_GC_REASON reason)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::SurvivingReferences(ULONG cSurvivingObjectIDRanges, ObjectID objectIDRangeStart[], ULONG cObjectIDRangeLength[])
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::GarbageCollectionFinished()
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::FinalizeableObjectQueued(DWORD finalizerFlags, ObjectID objectID)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::RootReferences2(ULONG cRootRefs, ObjectID rootRefIds[], COR_PRF_GC_ROOT_KIND rootKinds[], COR_PRF_GC_ROOT_FLAGS rootFlags[], UINT_PTR rootIds[])
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::HandleCreated(GCHandleID handleId, ObjectID initialObjectId)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::HandleDestroyed(GCHandleID handleId)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::InitializeForAttach(IUnknown *pProfilerInfoUnk, void *pvClientData, UINT cbClientData)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ProfilerAttachComplete()
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ProfilerDetachSucceeded()
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ReJITCompilationStarted(FunctionID functionId, ReJITID rejitId, BOOL fIsSafeToBlock)
    {
        if (debug) std::wcout << "ReJITCompilationStarted: starting ..." << std::endl;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::GetReJITParameters(ModuleID moduleId, mdMethodDef methodId, ICorProfilerFunctionControl *pFunctionControl)
    {
        if (debug) std::wcout << "GetReJITParameters: starting ..." << std::endl;

        auto hr = InnerRewrite("ReJitRewriteTarget"_W, moduleId, methodId, pFunctionControl);
        hr = InnerRewrite("ReJitRewriteTarget"_W, moduleId, methodId, pFunctionControl);

        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ReJITCompilationFinished(FunctionID functionId, ReJITID rejitId, HRESULT hrStatus, BOOL fIsSafeToBlock)
    {
        if (debug) std::wcout << "ReJITCompilationFinished: starting ..." << std::endl;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ReJITError(ModuleID moduleId, mdMethodDef methodId, FunctionID functionId, HRESULT hrStatus)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::MovedReferences2(ULONG cMovedObjectIDRanges, ObjectID oldObjectIDRangeStart[], ObjectID newObjectIDRangeStart[], SIZE_T cObjectIDRangeLength[])
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::SurvivingReferences2(ULONG cSurvivingObjectIDRanges, ObjectID objectIDRangeStart[], SIZE_T cObjectIDRangeLength[])
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ConditionalWeakTableElementReferences(ULONG cRootRefs, ObjectID keyRefIds[], ObjectID valueRefIds[], GCHandleID rootIds[])
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::GetAssemblyReferences(const WCHAR *wszAssemblyPath, ICorProfilerAssemblyReferenceProvider *pAsmRefProvider)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::ModuleInMemorySymbolsUpdated(ModuleID moduleId)
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::DynamicMethodJITCompilationStarted(FunctionID functionId, BOOL fIsSafeToBlock, LPCBYTE ilHeader, ULONG cbILHeader)
    { 
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Profiler::DynamicMethodJITCompilationFinished(FunctionID functionId, HRESULT hrStatus, BOOL fIsSafeToBlock)
    {
        return S_OK;
    }

    HRESULT Profiler::DoRequestReJit(WSTRING functionName)
    {
        FunctionMetaInfo* functionMetaInfo = nullptr;
        {
            std::lock_guard<std::mutex> guard(mapLock);
            if (functionNameMetaInfoMap.count(functionName) > 0) {
                functionMetaInfo = functionNameMetaInfoMap[functionName];
            }
        }

        if (functionMetaInfo == nullptr) {
            if (debug) std::wcout << "DoRequestReJit: Didn't find required meta data: " << std::endl;

            return S_OK;
        }

        int numberMethods = 1;
        ModuleID* moduleIds = new ModuleID[numberMethods] { functionMetaInfo->moduleId };
        mdMethodDef* methodIds = new mdMethodDef[numberMethods] { functionMetaInfo->functionToken };
        HRESULT hr = corProfilerInfo->RequestReJIT(numberMethods, moduleIds, methodIds);

        if (debug) std::wcout << "DoRequestReJit: result: " << std::hex << hr << std::dec << std::endl;

        return S_OK;
    }

    extern "C" __declspec(dllexport) HRESULT __cdecl RequestReJit(LPWSTR functionNameChar)
    {
        if (debug) std::wcout << "RequestReJit: starting ... " << functionNameChar << " !" << std::endl;

        auto profiler = Profiler::GetSingletonish();
        if (profiler == nullptr) {
            if (debug) std::wcout << "Unable to request rejit because the profiler reference is invalid." << std::endl;
            return E_FAIL;
        }
        WSTRING functionName(functionNameChar);

        std::thread t1(&Profiler::DoRequestReJit, profiler, functionName);

        // block the calling managed thread until the worker thread has finished
        t1.join();

        return S_OK;
    }
}
