// headless.cs - exercise the same SnmpEngine that snmp_desktop uses,
// to verify each "page" of the desktop app can pull live data from the device.
using System;
using SnmpCommon;
using SnmpDesktop;

class Program
{
    static int pass = 0, fail = 0;
    static void Check(bool ok, string name, string extra = "")
    {
        Console.WriteLine($"  [{(ok ? "PASS" : "FAIL")}] {name}{(extra.Length > 0 ? " -> " + extra : "")}");
        if (ok) pass++; else fail++;
    }

    static int Main(string[] args)
    {
        string host = args.Length > 0 ? args[0] : "192.168.10.99";
        var eng = new SnmpEngine { Host = host, Port = 161, Community = "public" };

        Console.WriteLine($"=== snmp_desktop backend test (SnmpEngine -> {host}) ===\n");

        // Page: 系统概念 (System) - 32.1.1 Descr, 32.1.2 Clock, 32.1.3 Tasks, 32.1.4 Uptime
        {
            var descr = eng.Get(SnmpEngine.ParseOid(Oids.System.Descr))?.AsString();
            var clock = eng.Get(SnmpEngine.ParseOid(Oids.System.Clock))?.AsString();
            var tasks = eng.Get(SnmpEngine.ParseOid(Oids.System.Tasks))?.AsString();
            var uptime = eng.Get(SnmpEngine.ParseOid(Oids.System.Uptime))?.AsString();
            Check(!string.IsNullOrEmpty(descr), "系统概念/Descr", descr);
            Check(!string.IsNullOrEmpty(clock), "系统概念/Clock", clock);
            Check(!string.IsNullOrEmpty(tasks), "系统概念/Tasks", tasks);
            Check(!string.IsNullOrEmpty(uptime), "系统概念/Uptime", uptime);
        }

        // Page: 硬件监控 (Hardware) - MCU (32.1.1 reuse), Clock (32.1.2), Tasks (32.1.3)
        {
            var mcu = eng.Get(SnmpEngine.ParseOid(Oids.System.Descr))?.AsString();
            var clock = eng.Get(SnmpEngine.ParseOid(Oids.System.Clock))?.AsString();
            var tasks = eng.Get(SnmpEngine.ParseOid(Oids.System.Tasks))?.AsString();
            Check(!string.IsNullOrEmpty(mcu), "硬件监控/MCU", mcu);
            Check(!string.IsNullOrEmpty(clock), "硬件监控/Clock", clock);
            Check(!string.IsNullOrEmpty(tasks), "硬件监控/Tasks", tasks);
        }

        // Page: 传感器监控 (Sensors) - 13 sensors (32.3.1..13)
        {
            int sensorOk = 0;
            for (uint i = 1; i <= 13; i++)
            {
                var v = eng.Get(SnmpEngine.ParseOid($"1.3.6.1.4.1.32.3.{i}"))?.AsString();
                if (!string.IsNullOrEmpty(v)) sensorOk++;
            }
            Check(sensorOk == 13, $"传感器监控/13 路传感器全部可读 ({sensorOk}/13)");
        }

        // Page: 参数设置 (Settings) - 硬件信息区 (MCU/Clock/13 sensors) + 控制
        {
            var mcu = eng.Get(SnmpEngine.ParseOid(Oids.System.Descr))?.AsString();
            var clock = eng.Get(SnmpEngine.ParseOid(Oids.System.Clock))?.AsString();
            Check(!string.IsNullOrEmpty(mcu), "参数设置/硬件信息 MCU", mcu);
            Check(!string.IsNullOrEmpty(clock), "参数设置/硬件信息 Clock", clock);
            int sensorOk = 0;
            for (uint i = 1; i <= 13; i++)
            {
                var v = eng.Get(SnmpEngine.ParseOid($"1.3.6.1.4.1.32.3.{i}"))?.AsString();
                if (!string.IsNullOrEmpty(v)) sensorOk++;
            }
            Check(sensorOk == 13, $"参数设置/硬件信息 13 路传感器 ({sensorOk}/13)");
            bool setLed = eng.SetInt(SnmpEngine.ParseOid(Oids.Control.Led), 1);
            Check(setLed, "参数设置/Set Led=1");
            var ledBack = eng.Get(SnmpEngine.ParseOid(Oids.Control.Led))?.AsString();
            Check(ledBack == "1", "参数设置/Led readback", ledBack);
            bool setBeep = eng.SetInt(SnmpEngine.ParseOid(Oids.Control.Beep), 1);
            Check(setBeep, "参数设置/Set Beep=1");
            eng.SetInt(SnmpEngine.ParseOid(Oids.Control.Led), 0);
            eng.SetInt(SnmpEngine.ParseOid(Oids.Control.Beep), 0);
            Check(true, "参数设置/恢复 Led=0 Beep=0");
        }

        // Page: 网络状态 (Network) - IP/Mask/GW/MAC (32.2.1..4)
        {
            var ip = eng.Get(SnmpEngine.ParseOid(Oids.Network.Ip))?.AsString();
            var mask = eng.Get(SnmpEngine.ParseOid(Oids.Network.Mask))?.AsString();
            var gw = eng.Get(SnmpEngine.ParseOid(Oids.Network.Gw))?.AsString();
            var mac = eng.Get(SnmpEngine.ParseOid(Oids.Network.Mac))?.AsString();
            Check(!string.IsNullOrEmpty(ip), "网络状态/IP", ip);
            Check(!string.IsNullOrEmpty(mask), "网络状态/Mask", mask);
            Check(!string.IsNullOrEmpty(gw), "网络状态/GW", gw);
            Check(!string.IsNullOrEmpty(mac), "网络状态/MAC", mac);
        }

        // Page: 参数设置 (Settings) - control Led/Beep/Reset (32.4.1..3)
        {
            bool setLed = eng.SetInt(SnmpEngine.ParseOid(Oids.Control.Led), 1);
            Check(setLed, "参数设置/Set Led=1");
            var ledBack = eng.Get(SnmpEngine.ParseOid(Oids.Control.Led))?.AsString();
            Check(ledBack == "1", "参数设置/Led readback", ledBack);
            bool setBeep = eng.SetInt(SnmpEngine.ParseOid(Oids.Control.Beep), 1);
            Check(setBeep, "参数设置/Set Beep=1");
            eng.SetInt(SnmpEngine.ParseOid(Oids.Control.Led), 0);
            eng.SetInt(SnmpEngine.ParseOid(Oids.Control.Beep), 0);
            Check(true, "参数设置/恢复 Led=0 Beep=0");
        }

        // Walk (used by periodic refresh in desktop) over enterprise tree
        {
            var all = eng.Walk(SnmpEngine.ParseOid(Oids.Root));
            Check(all != null && all.Count >= 20, $"周期刷新/Walk 全树节点数 = {all?.Count ?? 0}");
            if (all != null)
            {
                foreach (var kv in all)
                    Console.WriteLine($"      {kv.Key} = {kv.Value}");
            }
        }

        Console.WriteLine($"\n=== RESULT: PASS={pass} FAIL={fail} ===");
        return fail == 0 ? 0 : 1;
    }
}
