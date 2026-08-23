// MainForm.cs — SNMP v2c client GUI (Get / GetNext / Walk / Set).
//
//   Adaptive layout:
//     - top toolbar: TableLayoutPanel with AutoSize, one row, eight columns
//       (Agent IP / Port / Community / OID / Op / Send / Walk / Clear Log).
//       Each cell hosts a 2-row mini-panel (Label + Control) or an aligned
//       button. The whole toolbar auto-grows to fit content, and the form
//       has MinimumSize so it can never be crushed narrower than the layout.
//
//   No Designer file — UI is built in code (project style).

using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Windows.Forms;
using Microsoft.VisualBasic;
using SnmpCommon;

namespace SnmpClient
{
    public partial class MainForm : Form
    {
        private TextBox txtHost = null!, txtPort = null!, txtCommunity = null!, txtOid = null!;
        private ComboBox cmbOp = null!;
        private Button btnSend = null!, btnWalk = null!, btnClear = null!;
        private ListView lv = null!;
        private TextBox txtLog = null!;
        private int reqId = 1;

        public MainForm()
        {
            InitUI();
        }

        private void InitUI()
        {
            Text = "SNMP v2c Client — STM32F429 (1.3.6.1.4.1.32)";
            BackColor = SystemColors.ControlDark;        // dark theme
            ForeColor = Color.LightGray;
            ClientSize = new Size(1180, 620);
            MinimumSize = new Size(1100, 400);

            // ---- top toolbar: TableLayoutPanel that grows to fit content ----
            var top = new TableLayoutPanel
            {
                Dock = DockStyle.Top,
                AutoSize = true,
                AutoSizeMode = AutoSizeMode.GrowAndShrink,
                BackColor = Color.FromArgb(30, 30, 30),
                Padding = new Padding(8),
                ColumnCount = 8,
                RowCount = 1,
            };
            for (int i = 0; i < 8; i++)
                top.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            top.RowStyles.Add(new RowStyle(SizeType.AutoSize));

            txtHost = FieldText("Agent IP", "192.168.10.99", 140);
            txtPort = FieldText("Port", "161", 70);
            txtCommunity = FieldText("Community", "public", 110);
            txtOid = FieldText("OID", "1.3.6.1.4.1.32.1.1", 260);

            cmbOp = new ComboBox { Width = 130, DropDownStyle = ComboBoxStyle.DropDownList };
            cmbOp.Items.AddRange(new[] { "Get", "GetNext", "Set(Integer)" });
            cmbOp.SelectedIndex = 0;

            btnSend = new Button { Text = "Send", AutoSize = true };
            btnSend.Click += OnSend;

            btnWalk = new Button { Text = "Walk (subtree)", AutoSize = true };
            btnWalk.Click += OnWalk;

            btnClear = new Button { Text = "Clear Log", AutoSize = true };
            btnClear.Click += (s, e) => txtLog.Clear();

            top.Controls.Add(WrapLabeled("Agent IP", txtHost), 0, 0);
            top.Controls.Add(WrapLabeled("Port", txtPort), 1, 0);
            top.Controls.Add(WrapLabeled("Community", txtCommunity), 2, 0);
            top.Controls.Add(WrapLabeled("OID", txtOid), 3, 0);
            top.Controls.Add(WrapLabeled("Op", cmbOp), 4, 0);
            top.Controls.Add(WrapLabeled(null, btnSend), 5, 0);
            top.Controls.Add(WrapLabeled(null, btnWalk), 6, 0);
            top.Controls.Add(WrapLabeled(null, btnClear), 7, 0);

            Controls.Add(top);

            // ---- middle: listview (fills remaining space) ----
            lv = new ListView
            {
                Dock = DockStyle.Fill,
                View = View.Details,
                GridLines = true,
                BackColor = Color.FromArgb(20, 20, 20),
                ForeColor = Color.LightGray,
                FullRowSelect = true
            };
            lv.Columns.Add("OID", 280);
            lv.Columns.Add("Type", 90);
            lv.Columns.Add("Value", 360);
            Controls.Add(lv);

            // ---- bottom: log textbox ----
            txtLog = new TextBox
            {
                Dock = DockStyle.Bottom,
                Height = 140,
                Multiline = true,
                ScrollBars = ScrollBars.Vertical,
                BackColor = Color.FromArgb(10, 10, 10),
                ForeColor = Color.LightGreen,
                Font = new Font("Consolas", 9)
            };
            Controls.Add(txtLog);

            // listview gets the middle space (top docked, bottom docked)
            lv.BringToFront();
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

        // Build a compact "Label over Control" cell (label=null → just control,
        // top-padded so it visually aligns with sibling labeled inputs).
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
                // reserve label-height so the control aligns with sibling input rows
                lp.Padding = new Padding(0, 18, 0, 0);
                lp.Controls.Add(c, 0, 0);
            }
            return lp;
        }

        // ---- SNMP I/O ----
        private byte[]? Transact(byte[] req)
        {
            using var udp = new UdpClient();
            udp.Client.ReceiveTimeout = 2000;
            udp.Client.SendTimeout = 2000;
            udp.Connect(txtHost.Text.Trim(), int.Parse(txtPort.Text));
            udp.Send(req, req.Length);
            var ep = new IPEndPoint(IPAddress.Any, 0);
            try { return udp.Receive(ref ep); }
            catch (SocketException) { return null; }
        }

        private void AddRow(uint[] oid, SnmpValue v)
        {
            var it = new ListViewItem(SnmpCodec.OidToString(oid));
            it.SubItems.Add(TagName(v.Tag));
            it.SubItems.Add(v.AsString());
            lv.Items.Add(it);
        }

        private static string TagName(byte t)
        {
            return t switch
            {
                Ber.INTEGER => "INTEGER",
                Ber.OCTET_STRING => "OCTET STR",
                Ber.OID => "OID",
                Ber.NULL => "NULL",
                Ber.APP_IPADDRESS => "IPADDR",
                Ber.APP_COUNTER32 => "Counter32",
                Ber.APP_GAUGE32 => "Gauge32",
                Ber.APP_TIMETICKS => "TimeTicks",
                _ => $"0x{t:X2}"
            };
        }

        private void Log(string s) => txtLog.AppendText($"[{DateTime.Now:HH:mm:ss}] {s}\r\n");

        private void OnSend(object? s, EventArgs e)
        {
            try
            {
                var oid = ParseOid(txtOid.Text.Trim());
                var op = cmbOp.Text;
                byte pdu = op == "Get" ? Ber.CTX_GetRequest
                         : op == "GetNext" ? Ber.CTX_GetNextRequest
                         : Ber.CTX_SetRequest;
                byte[] pkt;
                if (op == "Set(Integer)")
                {
                    var v = new SnmpValue { Tag = Ber.INTEGER, IntValue = PromptInt() };
                    pkt = SnmpCodec.BuildSet(pdu, reqId, txtCommunity.Text.Trim(), oid, v);
                }
                else
                {
                    pkt = SnmpCodec.BuildRequest(pdu, reqId, txtCommunity.Text.Trim(),
                                                 new List<uint[]> { oid });
                }
                Log($"TX ({op}) {SnmpCodec.OidToString(oid)}");
                var resp = Transact(pkt);
                if (resp == null) { Log("!! timeout / no response"); return; }
                if (SnmpCodec.ParseMessage(resp, out byte pduType, out int rid, out int err, out int idx, out var vars, out string comm))
                {
                    if (err != 0) Log($"!! error-status={err} index={idx}");
                    foreach (var vb in vars) { AddRow(vb.Oid, vb.Value); Log($"RX {SnmpCodec.OidToString(vb.Oid)} = {vb.Value.AsString()}"); }
                }
                else Log("!! failed to parse response");
                reqId++;
            }
            catch (Exception ex) { Log("!! " + ex.Message); }
        }

        private void OnWalk(object? s, EventArgs e)
        {
            try
            {
                var baseOid = ParseOid(txtOid.Text.Trim());
                var next = (uint[])baseOid.Clone();
                int count = 0, fails = 0;
                Log($"-- WALK under {SnmpCodec.OidToString(baseOid)} --");
                while (true)
                {
                    var pkt = SnmpCodec.BuildRequest(Ber.CTX_GetNextRequest, reqId,
                                                    txtCommunity.Text.Trim(), new List<uint[]> { next });
                    var resp = Transact(pkt);
                    if (resp == null) { Log("!! timeout during walk"); fails++; break; }
                    if (!SnmpCodec.ParseMessage(resp, out _, out _, out int err, out _, out var vars, out _) || vars.Count == 0)
                    { Log("!! walk ended (parse/novar)"); break; }
                    if (err != 0) { Log($"!! error-status={err}"); fails++; break; }
                    var vb = vars[0];
                    // stop if we left the subtree
                    if (!IsUnder(baseOid, vb.Oid)) break;
                    AddRow(vb.Oid, vb.Value);
                    Log($"  {SnmpCodec.OidToString(vb.Oid)} = {vb.Value.AsString()}");
                    next = vb.Oid;
                    count++;
                    if (count > 200) { Log("!! safety cap 200 reached"); break; }
                    reqId++;
                }
                Log($"-- WALK done: {count} vars, {fails} fails --");
            }
            catch (Exception ex) { Log("!! " + ex.Message); }
        }

        private static bool IsUnder(uint[] root, uint[] oid)
        {
            if (oid.Length < root.Length) return false;
            for (int i = 0; i < root.Length; i++) if (oid[i] != root[i]) return false;
            return true;
        }

        private static uint[] ParseOid(string s)
        {
            var parts = s.Split('.');
            var a = new uint[parts.Length];
            for (int i = 0; i < parts.Length; i++) a[i] = uint.Parse(parts[i].Trim());
            return a;
        }

        private int PromptInt()
        {
            var r = MessageBox.Show("Set value (0 or 1) for INTEGER node:", "Set",
                                    MessageBoxButtons.OKCancel);
            if (r != DialogResult.OK) return 0;
            string? s = Microsoft.VisualBasic.Interaction.InputBox("Integer value:", "Set", "1");
            return int.TryParse(s, out int v) ? v : 0;
        }
    }
}