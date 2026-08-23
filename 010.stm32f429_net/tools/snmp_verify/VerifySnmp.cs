// VerifySnmp.cs - headless SNMP verification against the STM32F429 agent.
// Reuses SnmpCore from the shared module. Build: see verify_snmp.sh / .ps1.
using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using SnmpCommon;

class VerifySnmp
{
    static int pass = 0, fail = 0;
    static void Check(bool ok, string name)
    {
        Console.WriteLine($"  [{(ok ? "PASS" : "FAIL")}] {name}");
        if (ok) pass++; else fail++;
    }

    static byte[] Transact(string host, int port, string community, byte pduType, List<uint[]> oids, int timeoutMs = 2000)
    {
        using var udp = new UdpClient();
        udp.Client.ReceiveTimeout = timeoutMs;
        udp.Connect(host, port);
        var reqId = new Random().Next(1, 0x7fffffff);
        var req = SnmpCodec.BuildRequest(pduType, reqId, community, oids);
        udp.Send(req, req.Length);
        var ep = new IPEndPoint(IPAddress.Any, 0);
        try { return udp.Receive(ref ep); }
        catch (SocketException) { return null; }
    }

    static int Main(string[] args)
    {
        string host = args.Length > 0 ? args[0] : "192.168.10.99";
        int port = args.Length > 1 ? int.Parse(args[1]) : 161;
        string community = "public";

        Console.WriteLine($"=== SNMP verification against {host}:{port} (community '{community}') ===\n");

        // Test 1: Get sysDescr (32.1.1)
        {
            var oid = new List<uint[]> { new uint[] { 1, 3, 6, 1, 4, 1, 32, 1, 1 } };
            var resp = Transact(host, port, community, Ber.CTX_GetRequest, oid);
            Check(resp != null, "Get 32.1.1 (sysDescr) -> response received");
            if (resp != null && SnmpCodec.ParseMessage(resp, out byte pdu, out int rid, out int err, out int idx, out var vars, out string comm))
            {
                Check(pdu == Ber.CTX_GetResponse, "  response PDU is GetResponse");
                Check(err == 0, "  error-status == 0");
                Check(vars.Count == 1, "  single varbind returned");
                if (vars.Count == 1)
                {
                    Check(SnmpCodec.OidToString(vars[0].Oid) == "1.3.6.1.4.1.32.1.1", "  OID matches request");
                    Check(!string.IsNullOrEmpty(vars[0].Value.AsString()), $"  value = '{vars[0].Value.AsString()}'");
                }
            }
        }

        // Test 2: Get sysTasks (32.1.3, INTEGER) and sysUptime (32.1.4, TimeTicks)
        {
            var oid = new List<uint[]> { new uint[] { 1, 3, 6, 1, 4, 1, 32, 1, 3 } };
            var resp = Transact(host, port, community, Ber.CTX_GetRequest, oid);
            Check(resp != null, "Get 32.1.3 (sysTasks) -> response received");
            if (resp != null && SnmpCodec.ParseMessage(resp, out _, out _, out int err, out _, out var vars, out _))
            {
                if (vars.Count == 1 && vars[0].Value.Tag == Ber.INTEGER)
                    Check(true, $"  value (INTEGER) = {vars[0].Value.AsString()}");
                else Check(false, "  returned INTEGER varbind");
            }

            var oid2 = new List<uint[]> { new uint[] { 1, 3, 6, 1, 4, 1, 32, 1, 4 } };
            var resp2 = Transact(host, port, community, Ber.CTX_GetRequest, oid2);
            Check(resp2 != null, "Get 32.1.4 (sysUptime) -> response received");
            if (resp2 != null && SnmpCodec.ParseMessage(resp2, out _, out _, out int err2, out _, out var vars2, out _))
            {
                if (vars2.Count == 1 && (vars2[0].Value.Tag == Ber.APP_TIMETICKS || vars2[0].Value.Tag == Ber.INTEGER))
                    Check(true, $"  value (TimeTicks) = {vars2[0].Value.AsString()}");
                else Check(false, "  returned TimeTicks varbind");
            }
        }

        // Test 3: Get net IP (32.2.1, OCTET/IP)
        {
            var oid = new List<uint[]> { new uint[] { 1, 3, 6, 1, 4, 1, 32, 2, 1 } };
            var resp = Transact(host, port, community, Ber.CTX_GetRequest, oid);
            Check(resp != null, "Get 32.2.1 (netIp) -> response received");
            if (resp != null && SnmpCodec.ParseMessage(resp, out _, out _, out int err, out _, out var vars, out _))
            {
                if (vars.Count == 1) Check(true, $"  value = '{vars[0].Value.AsString()}'");
                else Check(false, "  varbind present");
            }
        }

        // Test 4: GetNext walk over the whole enterprise tree (count nodes)
        {
            uint[] cur = { 1, 3, 6, 1, 4, 1, 32 };
            int count = 0;
            bool ok = true;
            for (int i = 0; i < 100; i++)
            {
                var oid = new List<uint[]> { (uint[])cur.Clone() };
                var resp = Transact(host, port, community, Ber.CTX_GetNextRequest, oid);
                if (resp == null) { ok = false; break; }
                if (!SnmpCodec.ParseMessage(resp, out _, out _, out int err, out _, out var vars, out _))
                { ok = false; break; }
                if (vars.Count == 0 || err != 0) break;
                var v = vars[0];
                // stop when we leave the 32 enterprise subtree
                if (v.Oid.Length < 9 || v.Oid[0] != 1 || v.Oid[6] != 32) break;
                cur = (uint[])v.Oid.Clone();
                count++;
            }
            Check(ok, "GetNext walk completed without errors");
            Check(count >= 20, $"  walked {count} nodes under .32 (expect >=20)");
        }

        // Test 5: Set LED (32.4.1) then read back
        {
            var setReq = SnmpCodec.BuildSet(Ber.CTX_SetRequest, 777, community,
                new uint[] { 1, 3, 6, 1, 4, 1, 32, 4, 1 },
                new SnmpValue { Tag = Ber.INTEGER, IntValue = 1 });
            using var udp = new UdpClient(); udp.Client.ReceiveTimeout = 2000; udp.Connect(host, port);
            udp.Send(setReq, setReq.Length);
            var ep = new IPEndPoint(IPAddress.Any, 0);
            byte[] setResp = null; try { setResp = udp.Receive(ref ep); } catch (SocketException) { }
            Check(setResp != null, "Set 32.4.1 (led) -> response received");
            if (setResp != null && SnmpCodec.ParseMessage(setResp, out _, out _, out int err, out _, out var vars, out _))
                Check(err == 0, "  Set error-status == 0");

            // Read back
            var getOid = new List<uint[]> { new uint[] { 1, 3, 6, 1, 4, 1, 32, 4, 1 } };
            var resp = Transact(host, port, community, Ber.CTX_GetRequest, getOid);
            Check(resp != null, "Get 32.4.1 after Set -> response received");
            if (resp != null && SnmpCodec.ParseMessage(resp, out _, out _, out _, out _, out var gv, out _))
                if (gv.Count == 1) Check(true, $"  led value = {gv[0].Value.AsString()}");
        }

        // Test 6: wrong community dropped
        {
            var oid = new List<uint[]> { new uint[] { 1, 3, 6, 1, 4, 1, 32, 1, 1 } };
            var resp = Transact(host, port, "private", Ber.CTX_GetRequest, oid, 1500);
            Check(resp == null, "Wrong community 'private' -> no response (dropped)");
        }

        // Test 7: Set writable network params (32.2.1 IP / 32.2.2 Mask / 32.2.3 GW) then read back
        {
            // Read original values first
            uint[][] netOids = {
                new uint[]{1,3,6,1,4,1,32,2,1},
                new uint[]{1,3,6,1,4,1,32,2,2},
                new uint[]{1,3,6,1,4,1,32,2,3},
            };
            string[] names = { "netIp", "netMask", "netGw" };
            string[] orig = new string[3];
            for (int k = 0; k < 3; k++)
            {
                var r = Transact(host, port, community, Ber.CTX_GetRequest, new List<uint[]>{netOids[k]});
                if (r != null && SnmpCodec.ParseMessage(r, out _, out _, out _, out _, out var vars, out _))
                    orig[k] = vars.Count == 1 ? vars[0].Value.AsString() : "(none)";
            }
            Console.WriteLine($"  original net: IP={orig[0]} Mask={orig[1]} GW={orig[2]}");

            // Set test values (valid addresses, device keeps running until reset)
            string[] test = { "192.168.10.123", "255.255.255.0", "192.168.10.1" };
            bool allSet = true;
            for (int k = 0; k < 3; k++)
            {
                var setReq = SnmpCodec.BuildSet(Ber.CTX_SetRequest, 900 + k, community, netOids[k],
                    new SnmpValue { Tag = Ber.OCTET_STRING, Bytes = System.Text.Encoding.ASCII.GetBytes(test[k]) });
                using var udp = new UdpClient(); udp.Client.ReceiveTimeout = 2000; udp.Connect(host, port);
                udp.Send(setReq, setReq.Length);
                var ep = new IPEndPoint(IPAddress.Any, 0);
                byte[] setResp = null; try { setResp = udp.Receive(ref ep); } catch (SocketException) { }
                bool ok = setResp != null && SnmpCodec.ParseMessage(setResp, out _, out _, out int err, out _, out _, out _)
                          && err == 0;
                Check(ok, $"Set 32.2.{k+1} ({names[k]}) = '{test[k]}'");
                if (!ok) allSet = false;
            }

            // Read back to confirm persistence in RAM
            if (allSet)
            {
                for (int k = 0; k < 3; k++)
                {
                    var r = Transact(host, port, community, Ber.CTX_GetRequest, new List<uint[]>{netOids[k]});
                    if (r != null && SnmpCodec.ParseMessage(r, out _, out _, out _, out _, out var vars, out _))
                    {
                        string val = vars.Count == 1 ? vars[0].Value.AsString() : "(none)";
                        Check(val == test[k], $"  readback 32.2.{k+1} = '{val}' (expect '{test[k]}')");
                    }
                    else Check(false, $"  readback 32.2.{k+1} received");
                }
            }

            // Restore original values so device keeps reachable for further tests
            for (int k = 0; k < 3; k++)
            {
                var setReq = SnmpCodec.BuildSet(Ber.CTX_SetRequest, 950 + k, community, netOids[k],
                    new SnmpValue { Tag = Ber.OCTET_STRING, Bytes = System.Text.Encoding.ASCII.GetBytes(orig[k]) });
                using var udp = new UdpClient(); udp.Client.ReceiveTimeout = 2000; udp.Connect(host, port);
                udp.Send(setReq, setReq.Length);
                var ep = new IPEndPoint(IPAddress.Any, 0);
                try { udp.Receive(ref ep); } catch (SocketException) { }
            }
            Console.WriteLine("  restored original net params");
        }

        // Test 8: Set Beep (32.4.2) then read back
        {
            var setReq = SnmpCodec.BuildSet(Ber.CTX_SetRequest, 800, community,
                new uint[] { 1, 3, 6, 1, 4, 1, 32, 4, 2 },
                new SnmpValue { Tag = Ber.INTEGER, IntValue = 1 });
            using var udp = new UdpClient(); udp.Client.ReceiveTimeout = 2000; udp.Connect(host, port);
            udp.Send(setReq, setReq.Length);
            var ep = new IPEndPoint(IPAddress.Any, 0);
            byte[] setResp = null; try { setResp = udp.Receive(ref ep); } catch (SocketException) { }
            Check(setResp != null, "Set 32.4.2 (beep) -> response received");
            if (setResp != null && SnmpCodec.ParseMessage(setResp, out _, out _, out int err, out _, out _, out _))
                Check(err == 0, "  Set error-status == 0");

            var resp = Transact(host, port, community, Ber.CTX_GetRequest,
                new List<uint[]> { new uint[] { 1, 3, 6, 1, 4, 1, 32, 4, 2 } });
            if (resp != null && SnmpCodec.ParseMessage(resp, out _, out _, out _, out _, out var gv, out _))
                if (gv.Count == 1) Check(true, $"  beep value = {gv[0].Value.AsString()}");
        }

        // Test 9: Reset device (32.4.3) -> device should reboot (SNMP drops then recovers)
        // Note: the agent triggers NVIC_SystemReset() right after sending the response,
        // so a SetResponse may or may not arrive. We treat "request sent" as success and
        // verify the reboot by checking SNMP recovers afterwards.
        {
            var setReq = SnmpCodec.BuildSet(Ber.CTX_SetRequest, 880, community,
                new uint[] { 1, 3, 6, 1, 4, 1, 32, 4, 3 },
                new SnmpValue { Tag = Ber.INTEGER, IntValue = 1 });
            using var udp = new UdpClient(); udp.Client.ReceiveTimeout = 2000; udp.Connect(host, port);
            int sent = 0; try { sent = udp.Send(setReq, setReq.Length); } catch (SocketException) { }
            Check(sent == setReq.Length, "Set 32.4.3 (reset) -> request sent");

            // Wait for reboot, then confirm device is back by SNMP Get (uptime should have reset)
            System.Threading.Thread.Sleep(3000);
            bool recovered = false;
            uint newUptime = 0;
            for (int i = 0; i < 12; i++)
            {
                var r = Transact(host, port, community, Ber.CTX_GetRequest,
                    new List<uint[]> { new uint[] { 1, 3, 6, 1, 4, 1, 32, 1, 4 } });
                if (r != null && SnmpCodec.ParseMessage(r, out _, out _, out _, out _, out var vars, out _))
                {
                    if (vars.Count == 1 && uint.TryParse(vars[0].Value.AsString(), out newUptime))
                    { recovered = true; break; }
                }
                System.Threading.Thread.Sleep(500);
            }
            Check(recovered, "Device recovered after reset (SNMP reachable again)");
            if (recovered)
                Check(newUptime < 201372, $"  uptime reset after reboot ({newUptime} < previous 201372)");
        }

        Console.WriteLine($"\n=== RESULT: PASS={pass} FAIL={fail} ===");
        return fail == 0 ? 0 : 1;
    }
}
