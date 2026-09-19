using System;
using System.Diagnostics;
using System.ServiceProcess;
using System.Threading;

namespace UEFILoader.HostRunner
{
    internal sealed class GuestMailboxService : ServiceBase
    {
        private const string PowerShellPath = @"C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe";
        private const string WorkerPath = @"C:\ProgramData\UEFILoaderRunner\Invoke-UEFILoaderGuestInfo.ps1";
        private const string ConfigPath = @"C:\ProgramData\UEFILoaderRunner\guest-runner.config.json";

        private readonly ManualResetEvent stopping = new ManualResetEvent(false);
        private readonly object childLock = new object();
        private Thread supervisor;
        private Process child;

        internal GuestMailboxService()
        {
            ServiceName = "UEFILoaderGuestMailbox";
            CanStop = true;
            CanShutdown = true;
            AutoLog = false;
        }

        protected override void OnStart(string[] args)
        {
            supervisor = new Thread(SuperviseWorker);
            supervisor.IsBackground = true;
            supervisor.Name = "UEFILoader GuestInfo worker supervisor";
            supervisor.Start();
        }

        protected override void OnStop()
        {
            StopWorker();
        }

        protected override void OnShutdown()
        {
            StopWorker();
            base.OnShutdown();
        }

        private void StopWorker()
        {
            stopping.Set();
            lock (childLock)
            {
                if (child != null && !child.HasExited)
                {
                    try { child.Kill(); }
                    catch (InvalidOperationException) { }
                }
            }
            if (supervisor != null && supervisor.IsAlive)
            {
                supervisor.Join(TimeSpan.FromSeconds(10));
            }
        }

        private void SuperviseWorker()
        {
            while (!stopping.WaitOne(0))
            {
                Process process = null;
                try
                {
                    ProcessStartInfo start = new ProcessStartInfo();
                    start.FileName = PowerShellPath;
                    start.Arguments = "-NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" +
                        WorkerPath + "\" -ConfigPath \"" + ConfigPath + "\"";
                    start.WorkingDirectory = @"C:\ProgramData\UEFILoaderRunner";
                    start.UseShellExecute = false;
                    start.CreateNoWindow = true;

                    process = new Process();
                    process.StartInfo = start;
                    lock (childLock) { child = process; }
                    if (!process.Start())
                    {
                        throw new InvalidOperationException("The GuestInfo worker did not start.");
                    }
                    while (!process.WaitForExit(1000))
                    {
                        if (stopping.WaitOne(0))
                        {
                            try { process.Kill(); }
                            catch (InvalidOperationException) { }
                            break;
                        }
                    }
                }
                catch
                {
                    // The authenticated host heartbeat is the authoritative
                    // health signal. Retry locally without exposing secrets.
                }
                finally
                {
                    lock (childLock)
                    {
                        if (Object.ReferenceEquals(child, process)) { child = null; }
                    }
                    if (process != null) { process.Dispose(); }
                }

                if (stopping.WaitOne(TimeSpan.FromSeconds(2))) { break; }
            }
        }

        public static void Main()
        {
            ServiceBase.Run(new GuestMailboxService());
        }
    }
}
