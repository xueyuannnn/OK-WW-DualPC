using System;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Windows.Forms;

internal static class Launcher
{
    [STAThread]
    private static int Main(string[] args)
    {
        string root = AppDomain.CurrentDomain.BaseDirectory;
        string script = Path.Combine(root, "tools_dualpc", "run.ps1");
        StringBuilder flags = new StringBuilder();
        foreach (string arg in args)
        {
            switch (arg)
            {
                case "--check": flags.Append(" -Check"); break;
                case "--diagnose": flags.Append(" -Diagnose"); break;
                case "--setup": flags.Append(" -Configure"); break;
                case "--install-deps": flags.Append(" -Setup"); break;
                case "--check-update": flags.Append(" -UpdateCheck"); break;
                case "--prepare-update": flags.Append(" -PrepareUpdate"); break;
                default:
                    MessageBox.Show("不支持的启动参数：" + arg, "OK-WW-E 双机");
                    return 2;
            }
        }
        try
        {
            string directory = Path.Combine(root, "logs");
            Directory.CreateDirectory(directory);
            string log = Path.Combine(directory, "launcher-" + DateTime.Now.ToString("yyyyMMdd-HHmmss-fff") + ".log");
            using (StreamWriter writer = new StreamWriter(log, false, new UTF8Encoding(false)))
            using (Process process = new Process())
            {
                object gate = new object();
                process.StartInfo.FileName = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System),
                                                          "WindowsPowerShell", "v1.0", "powershell.exe");
                process.StartInfo.Arguments = "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + script + "\"" + flags;
                process.StartInfo.WorkingDirectory = root;
                process.StartInfo.UseShellExecute = false;
                process.StartInfo.CreateNoWindow = true;
                process.StartInfo.WindowStyle = ProcessWindowStyle.Hidden;
                process.StartInfo.RedirectStandardOutput = true;
                process.StartInfo.RedirectStandardError = true;
                process.StartInfo.StandardOutputEncoding = Encoding.UTF8;
                process.StartInfo.StandardErrorEncoding = Encoding.UTF8;
                process.StartInfo.EnvironmentVariables["PYTHONIOENCODING"] = "utf-8";
                DataReceivedEventHandler capture = delegate(object sender, DataReceivedEventArgs data)
                {
                    if (data.Data != null)
                        lock (gate) { writer.WriteLine(data.Data); writer.Flush(); }
                };
                process.OutputDataReceived += capture;
                process.ErrorDataReceived += capture;
                process.Start();
                process.BeginOutputReadLine();
                process.BeginErrorReadLine();
                process.WaitForExit();
                if (process.ExitCode != 0)
                    MessageBox.Show("软件启动或运行失败，请查看日志：\n" + log, "OK-WW-E 双机");
                return process.ExitCode;
            }
        }
        catch (Exception error)
        {
            MessageBox.Show("无法启动软件：\n" + error.Message, "OK-WW-E 双机");
            return 1;
        }
    }
}
