// MainForm.cs — SNMP v2c server / proxy tool.
//
//   Two modes (radio):
//     1) "Sniffer"  : bind UDP 161 locally, display every SNMP packet received
//                     (parsed PDU + VarBinds). Useful to watch the STM32 agent
//                     answer a third-party manager, or to debug the client.
//     2) "Proxy"    : bind UDP 161, forward each request to a real agent
//                     (e.g. 192.168.10.99:161) and relay its response back to
//                     the original manager. Lets you sit between a manager and
//                     the device and log/inspect the traffic.
//   Plus a "Send Trap" button that crafts a v2c Trap (0xA4) to a chosen target,
//   for testing Trap receivers.
//
//   This tool does NOT implement a MIB; it is a transport/parser + proxy so you
//   can validate the embedded agent end-to-end from the PC.
//
//   Layout: 2-row TableLayoutPanel toolbar with AutoSize, so all controls are
//   visible (no wrapping / clipping) and the form grows if the OS / font forces
//   larger sizes. MinimumSize prevents the user from crushing the layout.

using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Windows.Forms;
using SnmpCommon;

namespace SnmpServer
{
    public partial class MainForm : Form
    {
        private TextBox txtListenPort = null!, txtUpstreamHost = null!, txtUpstreamPort = null!, txtTrapTarget = null!, txtTrapPort = null!;
        private RadioButton rbSniff = null!, rbProxy = null!;
        private Button btnStart = null!, btnStop = null!, btnTrap = null!, btnClear = null!;
        private TextBox txtLog = null!;
        private ApiServer? _api;
        private TextBox txtApiPort = null!;
        private Button btnApiStart = null!, btnApiStop = null!;
        private UdpClient? _listener;
        private Thread? _thread;
        private volatile bool _running;
        private IPEndPoint? _lastManager;

        public MainForm() => InitUI();

        private void InitUI()
        {
            Text = "SNMP v2c Server / Proxy — PC side";
            BackColor = SystemColors.ControlDark; ForeColor = Color.LightGray;
            ClientSize = new Size(940, 600);
            MinimumSize = new Size(880, 380);

            // ---- top toolbar: 2-row TableLayoutPanel, AutoSize ----
            var top = new TableLayoutPanel
            {
                Dock = DockStyle.Top,
                AutoSize = true,
                AutoSizeMode = AutoSizeMode.GrowAndShrink,
                BackColor = Color.FromArgb(30, 30, 30),
                Padding = new Padding(8),
                ColumnCount = 7,
                RowCount = 2,
            };
            for (int i = 0; i < 7; i++)
                top.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            top.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            top.RowStyles.Add(new RowStyle(SizeType.AutoSize));

            txtListenPort = FieldText("Listen Port", "161", 70);
            rbSniff  = new RadioButton { Text = "Sniffer",      Checked = true, ForeColor = Color.LightGray, AutoSize = true };
            rbProxy  = new RadioButton { Text = "Proxy → Agent",                  ForeColor = Color.LightGray, AutoSize = true };
            txtUpstreamHost = FieldText("Upstream IP",   "192.168.10.99", 130);
            txtUpstreamPort = FieldText("Upstream Port", "161",           70);

            btnStart = new Button { Text = "Start", AutoSize = true }; btnStart.Click += OnStart;
            btnStop  = new Button { Text = "Stop",  AutoSize = true }; btnStop.Click  += OnStop; btnStop.Enabled = false;

            txtTrapTarget = FieldText("Trap Target", "192.168.10.5", 130);
            txtTrapPort   = FieldText("Trap Port",   "162",          70);
            btnTrap = new Button { Text = "Send Trap", AutoSize = true }; btnTrap.Click += OnTrap;

            btnClear = new Button { Text = "Clear Log", AutoSize = true };
            btnClear.Click += (s, e) => txtLog.Clear();

            // Row 1: Listen Port | Sniffer | Proxy→ | Upstream IP | Upstream Port | Start | Stop
            top.Controls.Add(WrapLabeled("Listen Port",   txtListenPort),   0, 0);
            top.Controls.Add(WrapLabeled(null,            rbSniff),         1, 0);
            top.Controls.Add(WrapLabeled(null,            rbProxy),         2, 0);
            top.Controls.Add(WrapLabeled("Upstream IP",   txtUpstreamHost), 3, 0);
            top.Controls.Add(WrapLabeled("Upstream Port", txtUpstreamPort), 4, 0);
            top.Controls.Add(WrapLabeled(null,            btnStart),        5, 0);
            top.Controls.Add(WrapLabeled(null,            btnStop),         6, 0);

            // Row 2: Trap Target | Trap Port | Send Trap | (empty cols 3,4 = spacer) | (col 5 empty) | Clear Log
            top.Controls.Add(WrapLabeled("Trap Target", txtTrapTarget), 0, 1);
            top.Controls.Add(WrapLabeled("Trap Port",   txtTrapPort),   1, 1);
            top.Controls.Add(WrapLabeled(null,          btnTrap),       2, 1);
            // col 3,4 left empty as horizontal filler
            top.Controls.Add(WrapLabeled(null,          btnClear),      6, 1);
            // col 5 row 2 also left empty (extra spacer)

            // ---- Row 3: HTTP API (lets snmp_desktop fetch SNMP data from this server) ----
            txtApiPort = FieldText("API Port", "8081", 70);
            btnApiStart = new Button { Text = "Start API", AutoSize = true }; btnApiStart.Click += OnApiStart;
            btnApiStop  = new Button { Text = "Stop API",  AutoSize = true }; btnApiStop.Click  += OnApiStop; btnApiStop.Enabled = false;
            top.RowCount = 3;
            top.Controls.Add(WrapLabeled("API Port", txtApiPort), 0, 2);
            top.Controls.Add(WrapLabeled(null, btnApiStart),      1, 2);
            top.Controls.Add(WrapLabeled(null, btnApiStop),       2, 2);

            Controls.Add(top);

            // ---- log fills remaining space ----
            txtLog = new TextBox
            {
                Dock = DockStyle.Fill,
                Multiline = true,
                ScrollBars = ScrollBars.Vertical,
                BackColor = Color.FromArgb(10, 10, 10),
                ForeColor = Color.LightGreen,
                Font = new Font("Consolas", 9)
            };
            Controls.Add(txtLog);
            txtLog.BringToFront();
        }

        // ---- UI helpers ----
        private static TextBox FieldText(string label, string val, int w)
        {
            var tb = new TextBox
            {
                Text = val,
                Width = w,
                BackColor = Color.FromArgb(40, 40, 40),
                ForeColor = Color.LightGray
            };
            return tb;
        }

        private static Control WrapLabeled(string? label, Control c)
        {
            var lp = new TableLayoutPanel
            {
                AutoSize = true,
                AutoSizeMode = AutoSizeMode.GrowAndShrink,
                Margin = new Padding(4, 0, 4, 0),
                ColumnCount = 1
            };
            if (!string.IsNullOrEmpty(label))
            {
                lp.RowCount = 2;
                lp.RowStyles.Add(new RowStyle(SizeType.AutoSize));
                lp.RowStyles.Add(new RowStyle(SizeType.AutoSize));
                var lbl = new Label
                {
                    Text = label,
                    ForeColor = Color.LightGray,
                    AutoSize = true,
                    Margin = new Padding(0, 0, 0, 2)
                };
                lp.Controls.Add(lbl, 0, 0);
                lp.Controls.Add(c, 0, 1);
            }
            else
            {
                lp.RowCount = 1;
                lp.RowStyles.Add(new RowStyle(SizeType.AutoSize));
                lp.Padding = new Padding(0, 18, 0, 0); // align with labeled siblings
                lp.Controls.Add(c, 0, 0);
            }
            return lp;
        }

        // ---- logging / runtime ----
        private void Log(string s) => txtLog.AppendText($"[{DateTime.Now:HH:mm:ss.fff}] {s}\r\n");

        private void OnStart(object? s, EventArgs e)
        {
            if (_running) return;
            int port = int.Parse(txtListenPort.Text);
            try
            {
                _listener = new UdpClient(port);
            }
            catch (SocketException ex)
            {
                Log($"!! bind failed: {ex.Message} (port in use by another SNMP service?)");
                return;
            }
            _running = true;
            _thread = new Thread(Loop) { IsBackground = true };
            _thread.Start();
            btnStart.Enabled = false; btnStop.Enabled = true;
            Log($"Listening on UDP {port}  mode={(rbProxy.Checked ? "Proxy" : "Sniffer")}");
        }

        private void OnStop(object? s, EventArgs e)
        {
            _running = false;
            try { _listener?.Close(); } catch { }
            btnStart.Enabled = true; btnStop.Enabled = false;
            Log("Stopped.");
        }

        private void Loop()
        {
            var ep = new IPEndPoint(IPAddress.Any, 0);
            while (_running)
            {
                try
                {
                    if (_listener == null) break;
                    var data = _listener.Receive(ref ep);
                    _lastManager = ep;
                    var from = ep.Address.ToString();
                    if (!SnmpCodec.ParseMessage(data, out byte pdu, out int rid, out int err, out int idx, out var vars, out string comm))
                    {
                        Log($"RX {from}:{ep.Port} {data.Length}B (unparseable)");
                        continue;
                    }
                    string opName = pdu switch
                    {
                        Ber.CTX_GetRequest => "Get",
                        Ber.CTX_GetNextRequest => "GetNext",
                        Ber.CTX_SetRequest => "Set",
                        Ber.CTX_GetResponse => "Response",
                        Ber.CTX_Trap => "Trap",
                        _ => $"0x{pdu:X2}"
                    };
                    Log($"RX {from}:{ep.Port} {opName} id={rid} comm='{comm}' vars={vars.Count}");
                    foreach (var vb in vars)
                        Log($"   OID {SnmpCodec.OidToString(vb.Oid)}  [{SnmpCodec.TagName(vb.Value.Tag)}] = {vb.Value.AsString()}");

                    if (rbProxy.Checked && (pdu == Ber.CTX_GetRequest || pdu == Ber.CTX_GetNextRequest || pdu == Ber.CTX_SetRequest))
                    {
                        // forward to upstream agent, relay response back to manager
                        try
                        {
                            using var up = new UdpClient();
                            up.Client.ReceiveTimeout = 2000;
                            up.Connect(txtUpstreamHost.Text.Trim(), int.Parse(txtUpstreamPort.Text));
                            up.Send(data, data.Length);
                            var uep = new IPEndPoint(IPAddress.Any, 0);
                            var resp = up.Receive(ref uep);
                            Log($"   -> forwarded, upstream returned {resp.Length}B");
                            _listener.Send(resp, resp.Length, ep);
                        }
                        catch (SocketException ex)
                        {
                            Log($"   !! upstream error: {ex.Message}");
                        }
                    }
                }
                catch (SocketException) { /* closed */ }
                catch (Exception ex) { Log("!! " + ex.Message); }
            }
        }

        private void OnTrap(object? s, EventArgs e)
        {
            try
            {
                // minimal v2c Trap: SEQUENCE { version, community, Trap(0xA4){ ... } }
                // We encode a Trap PDU with no varbinds (valid, just a notify).
                var msg = new List<byte>();
                var pdu = new List<byte>();
                var vbl = new List<byte>();
                SnmpCodec.Wrap(pdu, Ber.SEQUENCE, vbl);
                SnmpCodec.EncodeInteger(pdu, 1);    // request-id placeholder
                SnmpCodec.EncodeInteger(pdu, 0);    // error-status
                SnmpCodec.EncodeInteger(pdu, 0);    // error-index
                SnmpCodec.Wrap(msg, Ber.CTX_Trap, pdu);
                SnmpCodec.EncodeOctet(msg, Encoding.ASCII.GetBytes("public"));
                SnmpCodec.EncodeInteger(msg, Ber.VERSION_v2c);
                SnmpCodec.Wrap(msg, Ber.SEQUENCE, msg);

                using var udp = new UdpClient();
                var target = new IPEndPoint(IPAddress.Parse(txtTrapTarget.Text.Trim()), int.Parse(txtTrapPort.Text));
                udp.Send(msg.ToArray(), msg.Count, target);
                Log($"TX Trap -> {txtTrapTarget.Text}:{txtTrapPort.Text} ({msg.Count}B)");
            }
            catch (Exception ex) { Log("!! " + ex.Message); }
        }

        // ---- HTTP API handlers + accessors (used by snmp_desktop) ----
        private void OnApiStart(object? s, EventArgs e)
        {
            if (_api != null && _api.IsRunning) return;
            int port = int.Parse(txtApiPort.Text);
            _api = new ApiServer(this);
            try { _api.Start(port); }
            catch (Exception ex) { Log($"!! API start failed: {ex.Message}"); return; }
            btnApiStart.Enabled = false; btnApiStop.Enabled = true;
            Log($"HTTP API listening on http://localhost:{port}/  (snmp_desktop can fetch SNMP data from here)");
        }

        private void OnApiStop(object? s, EventArgs e)
        {
            _api?.Stop();
            btnApiStart.Enabled = true; btnApiStop.Enabled = false;
            Log("HTTP API stopped.");
        }

        // Accessors so ApiServer can read the upstream target from the UI.
        public string UpstreamHost => txtUpstreamHost.Text.Trim();
        public string UpstreamPort => txtUpstreamPort.Text.Trim();
        public string Community => "public";
    }
}