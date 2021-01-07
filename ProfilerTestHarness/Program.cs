using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading;

namespace ProfilerTestHarness
{
    class Program
    {
        [DllImport("Profiler.dll", CharSet = CharSet.Unicode)]
        private static extern void RequestReJit([MarshalAs(UnmanagedType.LPWStr)] string functionName);

        static void JitRewriteTarget()
        { }

        static void ReJitRewriteTarget()
        { }

        static unsafe void Main(string[] args)
        {
            Environment.SetEnvironmentVariable("PATH", $"{Environment.GetEnvironmentVariable("PATH")};{Environment.GetEnvironmentVariable("CORECLR_PROFILER_PATH")}");

            var envVars = new[] { "CORECLR_ENABLE_PROFILING", "CORECLR_PROFILER", "CORECLR_PROFILER_PATH" };
            foreach (var envVar in envVars)
            {
                var item = Environment.GetEnvironmentVariable(envVar);
                Console.WriteLine(item);
            }

            JitRewriteTarget();
            ReJitRewriteTarget();

            var programType = typeof(Program);
            var functionName = $"{programType.FullName}.{nameof(ReJitRewriteTarget)}";
            Console.WriteLine(functionName);
            RequestReJit(functionName);

            JitRewriteTarget();
            ReJitRewriteTarget();

            Console.WriteLine("Hello World!");
        }
    }
}
