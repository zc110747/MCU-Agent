// Program.cs — 嵌入式软件管理系统 (SNMP 桌面管理软件) 入口
using System;
using System.Windows.Forms;

namespace SnmpDesktop
{
    internal static class Program
    {
        [STAThread]
        static void Main()
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new MainForm());
        }
    }
}
