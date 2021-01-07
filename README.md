Profiler API Bug
================

Demonstrates a bug, or at least an odd behaviour, in the .NET Profiling API.

A difference in behaviour between `ICorProfilerInfo::SetILFunctionBody` and `ICorProfilerFunctionControl::SetILFunctionBody` exists. Changes written by `ICorProfilerInfo::SetILFunctionBody` are visible to `ICorProfilerInfo::GetILFunctionBody` where as changes written by `ICorProfilerFunctionControl::SetILFunctionBody` are not visible to `ICorProfilerInfo::GetILFunctionBody` (`ICorProfilerFunctionControl` has no equivalent of `GetILFunctionBody`).

This project demonstrates that difference. There are two projects:

- Profiler - profiler constructed just to demo this behaviour difference
- ProfilerTestHarness - C# executable that will show the behaviour difference if the profiler is attached

## ProfilerTestHarness

This executable has two empty methods that will be rewritten by the profiler, these are called `JitRewriteTarget` and `ReJitRewriteTarget`. Both methods will be rewritten *twice* by the profiler, injecting instructions to write "Hello from {method name}" to the console. `JitRewriteTarget` will be rewritten by the `JITCompilationStarted` method using `ICorProfilerInfo::GetILFunctionBody`. `ReJitRewriteTarget` will be rewritten by `GetReJITParameters` using `ICorProfilerFunctionControl::SetILFunctionBody`, as this method receives a pointer to `ICorProfilerFunctionControl` as a parameter.

As the methods are being rewritten *twice* I would expect both methods to output the their message to the console twice, but this is not the case. `JitRewriteTarget` outputs it's message twice where as `ReJitRewriteTarget` only outputs it's message once.

The output from the project when execute is:

```
Setup and check environment ...
1
{88E5B029-D6B4-4709-B445-03E9BDAB2FA2}
..\..\..\x64\Debug\Profiler.dll

First set of method calls ...
Hello from JitRewriteTarget!
Hello from JitRewriteTarget!

Request Rejit and second set of method calls ...
Hello from JitRewriteTarget!
Hello from JitRewriteTarget!
Hello from ReJitRewriteTarget!

Test over.
```
