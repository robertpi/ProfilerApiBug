using System;
using System.IO;

namespace ProfilerTestHarness
{
    class Program
    {
        static void Main(string[] args)
        {
            var envVars = new[] { "CORECLR_ENABLE_PROFILING", "CORECLR_PROFILER", "CORECLR_PROFILER_PATH" };
            foreach (var envVar in envVars)
            {
                var item = Environment.GetEnvironmentVariable(envVar);
                Console.WriteLine(item);
            }

            Console.WriteLine("Hello World!");
        }
    }
}
