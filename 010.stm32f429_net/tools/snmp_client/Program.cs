// Program.cs — WinForms entry point for the SNMP client.
using System;
using System.Windows.Forms;
using SnmpCommon;

namespace SnmpClient
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
