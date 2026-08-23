// ApiServer.cs — HTTP JSON API layer for the SNMP server / proxy tool.
//
//   This turns snmp_server into a programmable SNMP data backend. A web-style
//   frontend (snmp_desktop) can fetch SNMP data "from snmp_server" over HTTP.
//
//   Endpoints (all JSON, content-type application/json):
//     GET  /api/walk?root=1.3.6.1.4.1.32&host=192.168.10.99&port=161&community=public
//          -> walks the subtree, returns { ok, rows:[{oid,tag,value}] }
//     GET  /api/query?oid=1.3.6.1.4.1.32.1.1&host=...&port=...&community=...
//          -> single GetRequest, returns { ok, oid, tag, value } | { ok, error }
//     POST /api/set  body { oid, type:"int"|"octet", value, host, port, community }
//          -> SetRequest, returns { ok } | { ok:false, error }
//
//   If host/port/community are omitted they default to the upstream fields on
//   the main form (so the UI radio + upstream settings still drive the target).

using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Threading;
using SnmpCommon;

namespace SnmpServer
{
    public class ApiServer
    {
        private readonly HttpListener _listener = new HttpListener();
        private Thread? _thread;
        private volatile bool _running;
        private readonly MainForm _form;

        public int Port { get; private set; } = 8081;

        public ApiServer(MainForm form) => _form = form;

        public bool IsRunning => _running;

        public void Start(int port)
        {
            if (_running) return;
            Port = port;
            _listener.Prefixes.Clear();
            _listener.Prefixes.Add($"http://127.0.0.1:{port}/");
            _listener.Prefixes.Add($"http://localhost:{port}/");
            _listener.Start();
            _running = true;
            _thread = new Thread(Loop) { IsBackground = true };
            _thread.Start();
        }

        public void Stop()
        {
            _running = false;
            try { _listener.Stop(); } catch { }
        }

        private void Loop()
        {
            while (_running)
            {
                HttpListenerContext ctx;
                try { ctx = _listener.GetContext(); }
                catch { break; }
                ThreadPool.QueueUserWorkItem(_ => Handle(ctx));
            }
        }

        private void Handle(HttpListenerContext ctx)
        {
            try
            {
                var req = ctx.Request;
                var resp = ctx.Response;
                resp.Headers.Add("Access-Control-Allow-Origin", "*");
                resp.Headers.Add("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
                resp.Headers.Add("Access-Control-Allow-Headers", "Content-Type");
                if (req.HttpMethod == "OPTIONS") { resp.StatusCode = 204; resp.Close(); return; }

                string body;
                if (req.HttpMethod == "POST" && req.Url!.AbsolutePath == "/api/set")
                    body = HandleSet(req);
                else if (req.Url!.AbsolutePath == "/api/walk")
                    body = HandleWalk(req);
                else if (req.Url!.AbsolutePath == "/api/query")
                    body = HandleQuery(req);
                else if (req.Url!.AbsolutePath == "/" || req.Url!.AbsolutePath == "/health")
                    body = "{\"ok\":true}";
                else
                { resp.StatusCode = 404; body = "{\"ok\":false,\"error\":\"not found\"}"; }

                var bytes = Encoding.UTF8.GetBytes(body);
                resp.ContentType = "application/json; charset=utf-8";
                resp.ContentLength64 = bytes.Length;
                resp.OutputStream.Write(bytes, 0, bytes.Length);
                resp.Close();
            }
            catch (Exception ex)
            {
                try
                {
                    var e = Encoding.UTF8.GetBytes("{\"ok\":false,\"error\":" + JsonString(ex.Message) + "}");
                    ctx.Response.StatusCode = 500;
                    ctx.Response.ContentType = "application/json; charset=utf-8";
                    ctx.Response.OutputStream.Write(e, 0, e.Length);
                    ctx.Response.Close();
                }
                catch { }
            }
        }

        // ---- target resolution: explicit query params win, else main-form upstream ----
        private (string host, int port, string community) Resolve(HttpListenerRequest req)
        {
            var host = req.QueryString["host"] ?? _form.UpstreamHost;
            var portStr = req.QueryString["port"] ?? _form.UpstreamPort;
            var community = req.QueryString["community"] ?? _form.Community;
            int.TryParse(portStr, out int port);
            if (port <= 0) port = 161;
            return (host, port, community);
        }

        private string HandleQuery(HttpListenerRequest req)
        {
            var (host, port, community) = Resolve(req);
            var oidStr = req.QueryString["oid"] ?? "";
            if (string.IsNullOrWhiteSpace(oidStr))
                return "{\"ok\":false,\"error\":\"missing oid\"}";

            var oid = SnmpCoreOid(oidStr);
            var pkt = SnmpCodec.BuildRequest(Ber.CTX_GetRequest, 1, community, new List<uint[]> { oid });
            var (ok, vb) = SnmpExchange(host, port, pkt);
            if (!ok || vb == null)
                return "{\"ok\":false,\"error\":\"no response\"}";
            return JsonValue(oidStr, vb);
        }

        private string HandleWalk(HttpListenerRequest req)
        {
            var (host, port, community) = Resolve(req);
            var rootStr = req.QueryString["root"] ?? "1.3.6.1.4.1.32";
            var root = SnmpCoreOid(rootStr);

            var rows = new List<string>();
            uint[] current = root;
            int max = 200;
            for (int i = 0; i < max; i++)
            {
                var pkt = SnmpCodec.BuildRequest(Ber.CTX_GetNextRequest, 1, community, new List<uint[]> { current });
                var (ok, vb) = SnmpExchange(host, port, pkt);
                if (!ok || vb == null) break;
                var oidStr = SnmpCodec.OidToString(vb.Oid);
                // stop when we leave the requested subtree
                if (!oidStr.StartsWith(rootStr) || oidStr == rootStr) break;
                rows.Add(JsonRow(oidStr, vb));
                current = vb.Oid;
            }
            return "{\"ok\":true,\"rows\":[" + string.Join(",", rows) + "]}";
        }

        private string HandleSet(HttpListenerRequest req)
        {
            using var sr = new StreamReader(req.InputStream, Encoding.UTF8);
            var json = sr.ReadToEnd();
            string oidStr = "", type = "int", value = "", host = "", portStr = "", community = "";
            try
            {
                using var doc = JsonDocument.Parse(json);
                var r = doc.RootElement;
                oidStr = r.TryGetProperty("oid", out var o) ? o.GetString() ?? "" : "";
                type = r.TryGetProperty("type", out var t) ? t.GetString() ?? "int" : "int";
                value = r.TryGetProperty("value", out var v) ? v.GetString() ?? "" : "";
                host = r.TryGetProperty("host", out var h) ? h.GetString() ?? "" : "";
                portStr = r.TryGetProperty("port", out var p) ? p.GetString() ?? "" : "";
                community = r.TryGetProperty("community", out var c) ? c.GetString() ?? "" : "";
            }
            catch { return "{\"ok\":false,\"error\":\"bad json\"}"; }

            if (string.IsNullOrWhiteSpace(host)) host = _form.UpstreamHost;
            if (string.IsNullOrWhiteSpace(portStr)) portStr = _form.UpstreamPort;
            if (string.IsNullOrWhiteSpace(community)) community = _form.Community;
            int.TryParse(portStr, out int port); if (port <= 0) port = 161;

            var oid = SnmpCoreOid(oidStr);
            var val = new SnmpValue { Tag = Ber.INTEGER };
            if (type == "octet")
            {
                val.Tag = Ber.OCTET_STRING;
                val.Bytes = Encoding.ASCII.GetBytes(value);
            }
            else
            {
                val.Tag = Ber.INTEGER;
                int.TryParse(value, out int iv);
                val.IntValue = iv;
            }

            var pkt = SnmpCodec.BuildSet(Ber.CTX_SetRequest, 1, community, oid, val);
            var (ok, vb) = SnmpExchange(host, port, pkt);
            if (!ok) return "{\"ok\":false,\"error\":\"no response\"}";
            return "{\"ok\":true}";
        }

        // ---- SNMP transport via UDP ----
        private (bool ok, VarBind? vb) SnmpExchange(string host, int port, byte[] pkt)
        {
            try
            {
                using var udp = new UdpClient();
                udp.Client.ReceiveTimeout = 2000;
                udp.Connect(host.Trim(), port);
                udp.Send(pkt, pkt.Length);
                var ep = new IPEndPoint(IPAddress.Any, 0);
                var resp = udp.Receive(ref ep);
                if (!SnmpCodec.ParseMessage(resp, out byte pdu, out int rid, out int err, out int idx, out var vars, out string comm))
                    return (false, null);
                if (vars.Count == 0) return (false, null);
                return (true, vars[0]);
            }
            catch
            {
                return (false, null);
            }
        }

        // ---- helpers ----
        private static uint[] SnmpCoreOid(string s)
        {
            var parts = s.Split('.');
            var arcs = new uint[parts.Length];
            for (int i = 0; i < parts.Length; i++) arcs[i] = uint.Parse(parts[i]);
            return arcs;
        }

        private static string JsonRow(string oid, VarBind vb)
            => "{\"oid\":" + JsonString(oid) + ",\"tag\":" + JsonString(SnmpCodec.TagName(vb.Value.Tag)) + ",\"value\":" + JsonString(vb.Value.AsString()) + "}";

        private static string JsonValue(string oid, VarBind vb)
            => "{\"ok\":true,\"oid\":" + JsonString(oid) + ",\"tag\":" + JsonString(SnmpCodec.TagName(vb.Value.Tag)) + ",\"value\":" + JsonString(vb.Value.AsString()) + "}";

        private static string JsonString(string s)
            => "\"" + s.Replace("\\", "\\\\").Replace("\"", "\\\"").Replace("\n", "\\n").Replace("\r", "\\r") + "\"";
    }
}
