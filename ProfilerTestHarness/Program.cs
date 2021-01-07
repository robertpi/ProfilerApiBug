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
            SetupAndCheckEnvironment();


            Console.WriteLine("First set of method calls ...");

            JitRewriteTarget();
            ReJitRewriteTarget();

            Console.WriteLine();
            Console.WriteLine("Request Rejit and second set of method calls ...");
            var programType = typeof(Program);
            var functionName = $"{programType.FullName}.{nameof(ReJitRewriteTarget)}";
            RequestReJit(functionName);

            JitRewriteTarget();
            ReJitRewriteTarget();

            Console.WriteLine();
            Console.WriteLine("Test over.");
        }

        private static unsafe void SetupAndCheckEnvironment()
        {
            Console.WriteLine("Setup and check environment ...");

            Environment.SetEnvironmentVariable("PATH", $"{Environment.GetEnvironmentVariable("PATH")};{Environment.GetEnvironmentVariable("CORECLR_PROFILER_PATH")}");

            var envVars = new[] { "CORECLR_ENABLE_PROFILING", "CORECLR_PROFILER", "CORECLR_PROFILER_PATH" };
            foreach (var envVar in envVars)
            {
                var item = Environment.GetEnvironmentVariable(envVar);
                Console.WriteLine(item);
            }

            Console.WriteLine();
        }
    }
}
