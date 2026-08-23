// Program.cs — WinForms entry point for the SNMP server/proxy tool.
using System;
using System.Windows.Forms;
using SnmpCommon;

namespace SnmpServer
{
    internal static class Program
    {
        [STAThread]
        static void Main()
        {
            ApplicationConfiguration.Initialize();
            Application.Run(new MainForm());
        }
    }
}
