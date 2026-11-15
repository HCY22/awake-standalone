// Standalone build: telemetry is removed. These no-op types keep the upstream
// event classes compiling, but nothing is ever collected or sent.
using System;

namespace Microsoft.PowerToys.Telemetry
{
    public enum PartA_PrivTags : ulong
    {
        None = 0,
        ProductAndServiceUsage = 0x0000000002000000,
        ProductAndServicePerformance = 0x0000000001000000,
    }

    public sealed class PowerToysTelemetry
    {
        public static PowerToysTelemetry Log { get; } = new();

        public void WriteEvent<T>(T telemetryEvent)
        {
            // Intentionally does nothing.
        }
    }

    public sealed class ETWTrace : IDisposable
    {
        public void Dispose()
        {
        }
    }
}

namespace Microsoft.PowerToys.Telemetry.Events
{
    public class EventBase
    {
        public string EventName { get; set; } = string.Empty;
    }

    public interface IEvent
    {
        Microsoft.PowerToys.Telemetry.PartA_PrivTags PartA_PrivTags { get; }
    }
}
