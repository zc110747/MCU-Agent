// SnmpEngine.cs — SNMP v2c data layer for the desktop manager.
//
//   This is the same logic used by snmp_client (UDP transport + SnmpCore codec),
//   wrapped into a small reusable helper so the desktop app can Get / Walk / Set
//   against the embedded agent directly (no dependency on snmp_server).
//
//   OID layout (embedded MIB, enterprise root 1.3.6.1.4.1.32):
//     32.1  System     : 32.1.1 sysDescr, 32.1.3 sysTasks, 32.1.4 sysUptime
//     32.2  Network    : 32.2.1 netIp, 32.2.2 netMask, 32.2.3 netGw, 32.2.4 netMac
//     32.3  Sensors    : 32.3.1..32.3.13 (13 analog/IMU values)
//     32.4  Control    : 32.4.1 led, 32.4.2 beep
//     32.5  Stats      : 32.5.1 req, 32.5.2 err, 32.5.3 lastUpd

using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using SnmpCommon;

namespace SnmpDesktop
{
    public class SnmpEngine
    {
        public string Host { get; set; } = "192.168.10.99";
        public int Port { get; set; } = 161;
        public string Community { get; set; } = "public";

        private int _reqId = 1;

        private byte[]? Transact(byte[] req)
        {
            using var udp = new UdpClient();
            udp.Client.ReceiveTimeout = 2000;
            udp.Client.SendTimeout = 2000;
            try
            {
                udp.Connect(Host.Trim(), Port);
                udp.Send(req, req.Length);
                var ep = new IPEndPoint(IPAddress.Any, 0);
                return udp.Receive(ref ep);
            }
            catch (SocketException) { return null; }
        }

        public SnmpValue? Get(uint[] oid)
        {
            var pkt = SnmpCodec.BuildRequest(Ber.CTX_GetRequest, _reqId,
                Community, new List<uint[]> { oid });
            var resp = Transact(pkt);
            if (resp == null) return null;
            if (!SnmpCodec.ParseMessage(resp, out _, out _, out int err, out _, out var vars, out _)) return null;
            if (err != 0 || vars.Count == 0) return null;
            _reqId++;
            return vars[0].Value;
        }

        public bool SetInt(uint[] oid, int value)
        {
            var v = new SnmpValue { Tag = Ber.INTEGER, IntValue = value };
            var pkt = SnmpCodec.BuildSet(Ber.CTX_SetRequest, _reqId, Community, oid, v);
            var resp = Transact(pkt);
            if (resp == null) return false;
            if (!SnmpCodec.ParseMessage(resp, out _, out _, out int err, out _, out _, out _)) return false;
            _reqId++;
            return err == 0;
        }

        public bool SetString(uint[] oid, string value)
        {
            var bytes = System.Text.Encoding.ASCII.GetBytes(value);
            var v = new SnmpValue { Tag = Ber.OCTET_STRING, Bytes = bytes };
            var pkt = SnmpCodec.BuildSet(Ber.CTX_SetRequest, _reqId, Community, oid, v);
            var resp = Transact(pkt);
            if (resp == null) return false;
            if (!SnmpCodec.ParseMessage(resp, out _, out _, out int err, out _, out _, out _)) return false;
            _reqId++;
            return err == 0;
        }

        // 系统复位：Set Reset=1 触发设备软复位
        public bool ResetDevice() => SetInt(ParseOid(Oids.Control.Reset), 1);

        // Walk a subtree under baseOid; returns oid->value string map.
        public Dictionary<string, string> Walk(uint[] baseOid, int maxNodes = 200)
        {
            var map = new Dictionary<string, string>();
            var next = (uint[])baseOid.Clone();
            int count = 0;
            while (count < maxNodes)
            {
                var pkt = SnmpCodec.BuildRequest(Ber.CTX_GetNextRequest, _reqId,
                    Community, new List<uint[]> { next });
                var resp = Transact(pkt);
                if (resp == null) break;
                if (!SnmpCodec.ParseMessage(resp, out _, out _, out int err, out _, out var vars, out _)) break;
                if (err != 0 || vars.Count == 0) break;
                var vb = vars[0];
                if (!IsUnder(baseOid, vb.Oid)) break;
                var key = SnmpCodec.OidToString(vb.Oid);
                if (map.ContainsKey(key)) break; // loop guard
                map[key] = vb.Value.AsString();
                next = vb.Oid;
                count++;
                _reqId++;
            }
            return map;
        }

        public static uint[] ParseOid(string s)
        {
            var parts = s.Split('.');
            var a = new uint[parts.Length];
            for (int i = 0; i < parts.Length; i++) a[i] = uint.Parse(parts[i].Trim());
            return a;
        }

        private static bool IsUnder(uint[] root, uint[] oid)
        {
            if (oid.Length < root.Length) return false;
            for (int i = 0; i < root.Length; i++) if (oid[i] != root[i]) return false;
            return true;
        }
    }

    // OID 定义（与嵌入式 MIB 对应）
    public static class Oids
    {
        public const string Root = "1.3.6.1.4.1.32";

        public static class System
        {
            public const string Descr = Root + ".1.1";
            public const string Clock = Root + ".1.2";
            public const string Tasks = Root + ".1.3";
            public const string Uptime = Root + ".1.4";
        }
        public static class Network
        {
            public const string Ip = Root + ".2.1";
            public const string Mask = Root + ".2.2";
            public const string Gw = Root + ".2.3";
            public const string Mac = Root + ".2.4";
        }
        public static class Sensors
        {
            public static readonly string[] All = {
                Root + ".3.1",  Root + ".3.2",  Root + ".3.3",  Root + ".3.4",  Root + ".3.5",
                Root + ".3.6",  Root + ".3.7",  Root + ".3.8",  Root + ".3.9",  Root + ".3.10",
                Root + ".3.11", Root + ".3.12", Root + ".3.13",
            };
            public static readonly string[] Labels = {
                "光照", "气压", "红外", "加速度X", "加速度Y", "加速度Z",
                "陀螺X", "陀螺Y", "陀螺Z", "磁力X", "磁力Y", "磁力Z", "数据有效",
            };
        }
        public static class Control
        {
            public const string Led = Root + ".4.1";
            public const string Beep = Root + ".4.2";
            public const string Reset = Root + ".4.3";
        }
        public static class Stats
        {
            public const string Req = Root + ".5.1";
            public const string Err = Root + ".5.2";
            public const string LastUpd = Root + ".5.3";
            public const string Recover = Root + ".5.4";
        }
    }
}
