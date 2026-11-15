// Standalone replacement for PowerToys ManagedCommon.RunnerHelper.
// Only the "wait for a process to exit, then run a callback" part is needed by Awake.
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading.Tasks;

namespace ManagedCommon
{
    public static class RunnerHelper
    {
        private const uint SYNCHRONIZE = 0x00100000;
        private const uint INFINITE = 0xFFFFFFFF;

        public static void WaitForPowerToysRunner(int pid, Action act, [CallerMemberName] string memberName = "")
            => TryWaitForProcessExit(pid, act);

        public static bool TryWaitForProcessExit(int processId, Action onExit)
        {
            ArgumentNullException.ThrowIfNull(onExit);

            IntPtr handle = OpenProcess(SYNCHRONIZE, false, processId);
            if (handle == IntPtr.Zero)
            {
                Logger.LogError($"Could not open process {processId} for an exit notification.");
                return false;
            }

            Task.Run(() =>
            {
                uint result;
                try
                {
                    result = WaitForSingleObject(handle, INFINITE);
                }
                finally
                {
                    CloseHandle(handle);
                }

                if (result == 0)
                {
                    onExit();
                }
                else
                {
                    Logger.LogError($"Waiting for process {processId} to exit failed: {result}.");
                }
            });

            return true;
        }

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr OpenProcess(uint dwDesiredAccess, bool bInheritHandle, int dwProcessId);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern uint WaitForSingleObject(IntPtr hHandle, uint dwMilliseconds);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool CloseHandle(IntPtr hObject);
    }
}
