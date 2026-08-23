// MainForm.cs — 嵌入式软件管理系统（SNMP 桌面管理软件）
//
//   结构（绝对定位 + AutoScroll，避免 Dock/Fill 不生效）：
//     ┌────────────── 标题栏（Top=92）───────────────────────────┐
//     │  嵌入式软件管理系统 + SNMP v2c + 连接字段 + 启动/停止    │
//     ├──────┬──────────────────────────────────────────────────┤
//     │ 左侧 │ 右侧内容（点击菜单切换）：                          │
//     │ 菜单 │  系统概念 / 设备信息 / 硬件监控 / 传感器监控 /     │
//     │      │  网络状态 / 参数设置                                │
//     │      │  每个页面 = PageHeader(64px) + Scroll(绝对定位)     │
//     └──────┴──────────────────────────────────────────────────┘
//
//   数据/设置走 SnmpEngine（UDP + SnmpCore）。后台 Timer 周期性刷新当前页。

using System;
using System.Collections.Generic;
using System.Drawing;
using System.Windows.Forms;

namespace SnmpDesktop
{
    public partial class MainForm : Form
    {
        private SnmpEngine _snmp = new SnmpEngine();

        // ===== 标题栏控件 =====
        private TextBox txtHost = null!, txtPort = null!, txtCommunity = null!;
        private Button btnStart = null!, btnStop = null!;
        private Label lblStatus = null!;

        // ===== 左侧菜单（自绘 Panel + 垂直滚动） =====
        private Panel pnlMenu = null!;
        private readonly List<(string Text, string Page)> _menuItems = new()
        {
            ("系统概念", "system"),
            ("设备信息", "device"),
            ("硬件监控", "hardware"),
            ("传感器监控", "sensors"),
            ("网络状态", "network"),
            ("参数设置", "settings"),
        };
        private readonly List<Label> _menuLabels = new();
        private int _menuSel = 0;

        // ===== 右侧内容 =====
        private Panel pnlContent = null!;
        private Panel pnlSystem = null!, pnlDevice = null!, pnlHardware = null!;
        private Panel pnlNetwork = null!, pnlSensors = null!, pnlSettings = null!;

        // ===== 系统概念页 =====
        private Label valDescr = null!, valTasks = null!, valUptime = null!;

        // ===== 设备信息页 =====
        private Label valDevMcu = null!, valDevId = null!, valDevHse = null!;
        private Label valDevPllM = null!, valDevPllN = null!, valDevPllP = null!, valDevPllQ = null!;
        private Label valDevSysclk = null!, valDevHclk = null!, valDevPclk1 = null!, valDevPclk2 = null!;
        private Label valDevFlash = null!, valDevSram = null!;

        // ===== 硬件监控页 =====
        private Label valMcu = null!, valClock = null!, _hwTasks = null!;

        // ===== 传感器监控页 =====
        private readonly Label[] _sensorVals = new Label[13];

        // ===== 网络状态页 =====
        private Label valIp = null!, valMask = null!, valGw = null!, valMac = null!;

        // ===== 参数设置页 =====
        private ToggleSwitch tgLed = null!, tgBeep = null!;
        private Label valLed = null!, valBeep = null!;
        private Label valSetMcu = null!, valSetClock = null!;
        private readonly Label[] _setSensorVals = new Label[13];
        private Label valReq = null!, valErr = null!, valLastUpd = null!;
        private NumericUpDown numInterval = null!;
        private TextBox txtNetIp = null!, txtNetMask = null!, txtNetGw = null!;
        private Button btnApplyNet = null!, btnReset = null!;

        private System.Threading.Timer? _timer;
        private volatile bool _running;
        private string _currentPage = "system";

        public MainForm() => InitUI();

        // ===================== 初始化 =====================
        private void InitUI()
        {
            Text = "嵌入式软件管理系统";
            BackColor = Color.FromArgb(15, 17, 21);
            ForeColor = Color.LightGray;
            ClientSize = new Size(980, 640);
            MinimumSize = new Size(820, 520);

            BuildTitleBar();
            BuildBody();
            SelectMenu(0);
        }

        private void BuildTitleBar()
        {
            var title = new Panel
            {
                Dock = DockStyle.Top,
                Height = 92,
                BackColor = Color.FromArgb(26, 29, 36),
            };
            Controls.Add(title);

            title.Controls.Add(new Label
            {
                Text = "嵌入式软件管理系统",
                Font = new Font("Microsoft YaHei", 16, FontStyle.Bold),
                ForeColor = Color.White,
                Location = new Point(16, 10),
                AutoSize = true,
            });
            title.Controls.Add(new Label
            {
                Text = "SNMP v2c · 设备 1.3.6.1.4.1.32 · STM32F429",
                ForeColor = Color.FromArgb(138, 144, 154),
                Location = new Point(18, 38),
                AutoSize = true,
                Font = new Font("Microsoft YaHei", 9),
            });

            txtHost = Field(title, "设备IP", "192.168.10.99", 120, 70);
            txtPort = Field(title, "端口", "161", 56, 200);
            txtCommunity = Field(title, "团体", "public", 80, 264);

            btnStart = new Button { Text = "启动轮询", Location = new Point(356, 64), Width = 92, Height = 24, BackColor = Color.FromArgb(76, 141, 255), ForeColor = Color.White, FlatStyle = FlatStyle.Flat };
            btnStop = new Button { Text = "停止", Location = new Point(456, 64), Width = 72, Height = 24, Enabled = false, FlatStyle = FlatStyle.Flat };
            btnStart.Click += OnStart;
            btnStop.Click += OnStop;
            title.Controls.Add(btnStart);
            title.Controls.Add(btnStop);

            lblStatus = new Label
            {
                Text = "● 空闲",
                ForeColor = Color.FromArgb(138, 144, 154),
                Location = new Point(540, 68),
                AutoSize = true,
                Font = new Font("Microsoft YaHei", 10),
            };
            title.Controls.Add(lblStatus);
        }

        private static TextBox Field(Panel parent, string label, string val, int w, int x)
        {
            parent.Controls.Add(new Label
            {
                Text = label,
                ForeColor = Color.FromArgb(138, 144, 154),
                Location = new Point(x, 42),
                AutoSize = true,
                Font = new Font("Microsoft YaHei", 9),
            });
            var tb = new TextBox
            {
                Text = val,
                Width = w,
                Location = new Point(x, 64),
                BackColor = Color.FromArgb(40, 44, 52),
                ForeColor = Color.LightGray,
                BorderStyle = BorderStyle.FixedSingle,
            };
            parent.Controls.Add(tb);
            return tb;
        }

        // ===================== 主体（绝对定位） =====================
        private void BuildBody()
        {
            // 左菜单：固定 200 宽，绝对定位
            pnlMenu = new Panel
            {
                Location = new Point(0, 92),
                Size = new Size(200, ClientSize.Height - 92),
                BackColor = Color.FromArgb(22, 25, 31),
                AutoScroll = true,
                Padding = new Padding(0, 12, 0, 12),
            };
            Controls.Add(pnlMenu);

            int y = 12;
            foreach (var (text, page) in _menuItems)
            {
                var lbl = new Label
                {
                    Text = text,
                    Location = new Point(0, y),
                    Size = new Size(200, 44),
                    ForeColor = Color.FromArgb(160, 166, 176),
                    BackColor = Color.Transparent,
                    Font = new Font("Microsoft YaHei", 12),
                    TextAlign = ContentAlignment.MiddleLeft,
                    Padding = new Padding(20, 0, 0, 0),
                    Tag = page,
                    Cursor = Cursors.Hand,
                };
                lbl.Click += (s, e) => SelectMenu(_menuItems.FindIndex(m => m.Page == (string)lbl.Tag));
                pnlMenu.Controls.Add(lbl);
                _menuLabels.Add(lbl);
                y += 48;
            }

            // 右内容：绝对定位
            pnlContent = new Panel
            {
                Location = new Point(200, 92),
                Size = new Size(ClientSize.Width - 200, ClientSize.Height - 92),
                BackColor = Color.FromArgb(15, 17, 21),
            };
            Controls.Add(pnlContent);

            // 窗口尺寸变化时同步
            Resize += (s, e) =>
            {
                int h = ClientSize.Height - 92;
                int w = ClientSize.Width - 200;
                pnlMenu.Size = new Size(200, Math.Max(200, h));
                pnlContent.Size = new Size(Math.Max(400, w), Math.Max(300, h));
                SyncScrollSizes();
                pnlContent.Refresh();
            };

            BuildSystemPage();
            BuildDevicePage();
            BuildHardwarePage();
            BuildSensorsPage();
            BuildNetworkPage();
            BuildSettingsPage();
        }

        private void SyncScrollSizes()
        {
            foreach (var p in new[] { pnlSystem, pnlDevice, pnlHardware, pnlSensors, pnlNetwork, pnlSettings })
            {
                if (p == null) continue;
                foreach (Control c in p.Controls)
                    if (c is Panel sc && sc.AutoScroll)
                        sc.Size = new Size(pnlContent.Width, pnlContent.Height - 64);
            }
        }

        // 选中菜单
        private void SelectMenu(int idx)
        {
            _menuSel = idx;
            for (int i = 0; i < _menuLabels.Count; i++)
            {
                var sel = i == idx;
                _menuLabels[i].ForeColor = sel ? Color.White : Color.FromArgb(160, 166, 176);
                _menuLabels[i].Font = new Font("Microsoft YaHei", 12, sel ? FontStyle.Bold : FontStyle.Regular);
                _menuLabels[i].BackColor = sel ? Color.FromArgb(34, 38, 47) : Color.Transparent;
                _menuLabels[i].Invalidate();
            }
            SwitchPage(_menuItems[idx].Page);
        }

        // ===================== 页面构建 =====================
        private void BuildSystemPage()
        {
            pnlSystem = new Panel { Dock = DockStyle.Fill, Visible = false };
            pnlSystem.Controls.Add(PageHeader("系统概念", "设备基础信息与运行状态"));
            var scroll = MakeScroll();
            pnlSystem.Controls.Add(scroll);
            int y = 8;
            valDescr = AddInfoCard(scroll, "设备描述", Oids.System.Descr, ref y);
            valTasks = AddInfoCard(scroll, "运行任务数", Oids.System.Tasks, ref y);
            valUptime = AddInfoCard(scroll, "系统运行时间", Oids.System.Uptime, ref y);
            pnlContent.Controls.Add(pnlSystem);
        }

        private void BuildDevicePage()
        {
            pnlDevice = new Panel { Dock = DockStyle.Fill, Visible = false };
            pnlDevice.Controls.Add(PageHeader("设备信息", "MCU 型号 · 时钟树 · 片上存储 (STM32F429IGT6)"));
            var scroll = MakeScroll();
            pnlDevice.Controls.Add(scroll);
            int y = 8;
            valDevMcu = AddInfoCard(scroll, "MCU 型号", Oids.System.Descr, ref y);
            valDevId = AddInfoCard(scroll, "芯片唯一 ID", "0x32FF...", ref y);
            valDevHse = AddInfoCard(scroll, "外部晶振 HSE", "25 MHz", ref y);
            valDevPllM = AddInfoCard(scroll, "PLL 分频 M", "25", ref y);
            valDevPllN = AddInfoCard(scroll, "PLL 倍频 N", "360", ref y);
            valDevPllP = AddInfoCard(scroll, "PLL 系统分频 P", "2 (SYSCLK)", ref y);
            valDevPllQ = AddInfoCard(scroll, "PLL Q 分频", "8 (仅供外设)", ref y);
            valDevSysclk = AddInfoCard(scroll, "系统时钟 SYSCLK", Oids.System.Clock, ref y);
            valDevHclk = AddInfoCard(scroll, "HCLK (AHB)", "180 MHz", ref y);
            valDevPclk1 = AddInfoCard(scroll, "PCLK1 (APB1)", "45 MHz", ref y);
            valDevPclk2 = AddInfoCard(scroll, "PCLK2 (APB2)", "90 MHz", ref y);
            valDevFlash = AddInfoCard(scroll, "Flash 容量", "1024 KB", ref y);
            valDevSram = AddInfoCard(scroll, "SRAM 容量", "192 KB + 64 KB CCM", ref y);
            pnlContent.Controls.Add(pnlDevice);
        }

        private void BuildHardwarePage()
        {
            pnlHardware = new Panel { Dock = DockStyle.Fill, Visible = false };
            pnlHardware.Controls.Add(PageHeader("硬件监控", "设备硬件概要与运行状态"));
            var scroll = MakeScroll();
            pnlHardware.Controls.Add(scroll);
            int y = 8;
            valMcu = AddInfoCard(scroll, "MCU 型号", Oids.System.Descr, ref y);
            valClock = AddInfoCard(scroll, "系统时钟", Oids.System.Clock, ref y);
            _hwTasks = AddInfoCard(scroll, "运行任务数", Oids.System.Tasks, ref y);
            pnlContent.Controls.Add(pnlHardware);
        }

        private void BuildSensorsPage()
        {
            pnlSensors = new Panel { Dock = DockStyle.Fill, Visible = false };
            pnlSensors.Controls.Add(PageHeader("传感器监控", "13 路传感器实时数据（光照 / 气压 / IMU）"));
            var scroll = MakeScroll();
            pnlSensors.Controls.Add(scroll);

            // 等宽双列网格（按 scroll 宽度自适应）
            int cardH = 80, gap = 12, pad = 16;
            int usableW = ClientSize.Width - 200 - 2;  // 减左菜单 200 + 2px 边框
            // 先定列数：每列至少 220 宽
            int cols = Math.Max(1, (usableW - pad * 2 + gap) / (220 + gap));
            if (cols > 4) cols = 4;
            int cw = (usableW - pad * 2 - (cols - 1) * gap) / cols;
            for (int i = 0; i < 13; i++)
            {
                int row = i / cols, col = i % cols;
                int cx = pad + col * (cw + gap);
                int cy = pad + row * (cardH + gap);
                var card = new Panel
                {
                    Location = new Point(cx, cy),
                    Size = new Size(cw, cardH),
                    BackColor = Color.FromArgb(26, 29, 36),
                };
                card.Controls.Add(new Label
                {
                    Text = Oids.Sensors.Labels[i],
                    ForeColor = Color.FromArgb(138, 144, 154),
                    Location = new Point(14, 10),
                    AutoSize = true,
                    Font = new Font("Microsoft YaHei", 10),
                });
                var val = new Label
                {
                    Text = "—",
                    ForeColor = Color.White,
                    Location = new Point(14, 32),
                    AutoSize = true,
                    Font = new Font("Microsoft YaHei", 16, FontStyle.Bold),
                };
                card.Controls.Add(val);
                _sensorVals[i] = val;
                scroll.Controls.Add(card);
            }
            pnlContent.Controls.Add(pnlSensors);
        }

        private void BuildNetworkPage()
        {
            pnlNetwork = new Panel { Dock = DockStyle.Fill, Visible = false };
            pnlNetwork.Controls.Add(PageHeader("网络状态", "IP / 掩码 / 网关 / MAC"));
            var scroll = MakeScroll();
            pnlNetwork.Controls.Add(scroll);
            int y = 8;
            valIp = AddInfoCard(scroll, "IP 地址", Oids.Network.Ip, ref y);
            valMask = AddInfoCard(scroll, "子网掩码", Oids.Network.Mask, ref y);
            valGw = AddInfoCard(scroll, "网关", Oids.Network.Gw, ref y);
            valMac = AddInfoCard(scroll, "MAC 地址", Oids.Network.Mac, ref y);
            pnlContent.Controls.Add(pnlNetwork);
        }

        private void BuildSettingsPage()
        {
            pnlSettings = new Panel { Dock = DockStyle.Fill, Visible = false };
            pnlSettings.Controls.Add(PageHeader("参数设置", "设备控制、网络设置、硬件信息与运行统计"));
            var scroll = MakeScroll();
            pnlSettings.Controls.Add(scroll);

            int y = 16;
            // 设备控制
            var ctrlCard = MakeCard("设备控制", 380, 130, ref y);
            tgLed = new ToggleSwitch { Location = new Point(14, 48), State = false, Caption = "LED 指示灯" };
            tgLed.Click += (s, e) => OnToggle(tgLed, Oids.Control.Led, valLed);
            valLed = new Label { Text = "状态：—", ForeColor = Color.FromArgb(138, 144, 154), Location = new Point(190, 54), AutoSize = true };
            ctrlCard.Controls.Add(tgLed); ctrlCard.Controls.Add(valLed);
            tgBeep = new ToggleSwitch { Location = new Point(14, 88), State = false, Caption = "蜂鸣器 BEEP" };
            tgBeep.Click += (s, e) => OnToggle(tgBeep, Oids.Control.Beep, valBeep);
            valBeep = new Label { Text = "状态：—", ForeColor = Color.FromArgb(138, 144, 154), Location = new Point(190, 94), AutoSize = true };
            ctrlCard.Controls.Add(tgBeep); ctrlCard.Controls.Add(valBeep);
            scroll.Controls.Add(ctrlCard);

            y += 10;
            // 网络设置
            var netCard = MakeCard("网络设置（修改后重启生效）", 380, 188, ref y);
            txtNetIp = NetField(netCard, "IP 地址", 14, 44);
            txtNetMask = NetField(netCard, "子网掩码", 14, 80);
            txtNetGw = NetField(netCard, "网关", 14, 116);
            netCard.Controls.Add(txtNetIp); netCard.Controls.Add(txtNetMask); netCard.Controls.Add(txtNetGw);
            btnApplyNet = new Button { Text = "应用网络设置", Location = new Point(14, 152), Width = 140, Height = 26, BackColor = Color.FromArgb(76, 141, 255), ForeColor = Color.White, FlatStyle = FlatStyle.Flat };
            btnApplyNet.Click += OnApplyNet;
            netCard.Controls.Add(btnApplyNet);
            scroll.Controls.Add(netCard);

            y += 10;
            // 系统维护
            var rstCard = MakeCard("系统维护", 380, 76, ref y);
            btnReset = new Button { Text = "系统复位（软重启）", Location = new Point(14, 42), Width = 160, Height = 26, BackColor = Color.FromArgb(255, 107, 107), ForeColor = Color.White, FlatStyle = FlatStyle.Flat };
            btnReset.Click += OnReset;
            rstCard.Controls.Add(btnReset);
            scroll.Controls.Add(rstCard);

            y += 10;
            // 硬件信息（MCU + 时钟 + 13 路传感器）
            var hwCard = MakeCard("硬件信息", 380, 460, ref y);
            valSetMcu = AddMini(hwCard, "MCU 型号", "—", 44);
            valSetClock = AddMini(hwCard, "系统时钟", "—", 72);
            hwCard.Controls.Add(new Label { Text = "传感器实时数据", ForeColor = Color.FromArgb(138, 144, 154), Location = new Point(14, 104), AutoSize = true, Font = new Font("Microsoft YaHei", 10, FontStyle.Bold) });
            for (int i = 0; i < 13; i++)
            {
                int sy = 130 + i * 24;
                hwCard.Controls.Add(new Label { Text = Oids.Sensors.Labels[i], ForeColor = Color.FromArgb(138, 144, 154), Location = new Point(20, sy), AutoSize = true, Font = new Font("Microsoft YaHei", 9) });
                var sv = new Label { Text = "—", ForeColor = Color.White, Location = new Point(180, sy), AutoSize = true, Font = new Font("Microsoft YaHei", 9, FontStyle.Bold) };
                hwCard.Controls.Add(sv);
                _setSensorVals[i] = sv;
            }
            scroll.Controls.Add(hwCard);

            y += 10;
            // 运行统计
            var statCard = MakeCard("运行统计", 380, 150, ref y);
            valReq = AddMini(statCard, "请求计数", "—", 44);
            valErr = AddMini(statCard, "错误计数", "—", 78);
            valLastUpd = AddMini(statCard, "最后更新", "—", 112);
            scroll.Controls.Add(statCard);

            y += 10;
            // 轮询设置
            var optCard = MakeCard("轮询设置", 380, 76, ref y);
            optCard.Controls.Add(new Label { Text = "轮询间隔(ms)", ForeColor = Color.FromArgb(138, 144, 154), Location = new Point(14, 44), AutoSize = true });
            numInterval = new NumericUpDown { Location = new Point(140, 40), Width = 90, Minimum = 500, Maximum = 10000, Increment = 500, Value = 3000, BackColor = Color.FromArgb(40, 44, 52), ForeColor = Color.LightGray };
            optCard.Controls.Add(numInterval);
            scroll.Controls.Add(optCard);

            pnlContent.Controls.Add(pnlSettings);
        }

        // ===================== UI 部件 =====================
        // 创建一个绝对定位的 PageHeader（64px 高）+ 可滚动 Scroll 区
        private static Panel PageHeader(string title, string sub)
        {
            var p = new Panel
            {
                Dock = DockStyle.Top,
                Height = 64,
                BackColor = Color.FromArgb(15, 17, 21),
            };
            p.Controls.Add(new Label
            {
                Text = title,
                ForeColor = Color.White,
                Font = new Font("Microsoft YaHei", 18, FontStyle.Bold),
                Location = new Point(16, 12),
                AutoSize = true,
            });
            p.Controls.Add(new Label
            {
                Text = sub,
                ForeColor = Color.FromArgb(138, 144, 154),
                Location = new Point(18, 42),
                AutoSize = true,
                Font = new Font("Microsoft YaHei", 9),
            });
            return p;
        }

        // 创建一个绝对定位的 Scroll 区（在 PageHeader 之下）
        private Panel MakeScroll()
        {
            return new Panel
            {
                Location = new Point(0, 64),
                Size = new Size(pnlContent.Width, pnlContent.Height - 64),
                AutoScroll = true,
                BackColor = Color.FromArgb(15, 17, 21),
                Padding = new Padding(0),
            };
        }

        // 信息卡片（绝对定位，纵向堆叠，y 自动累加）
        private static Label AddInfoCard(Panel parent, string label, string oid, ref int y)
        {
            var card = new Panel
            {
                Location = new Point(16, y),
                Size = new Size(parent.ClientSize.Width > 0 ? parent.ClientSize.Width - 32 : 400, 100),
                BackColor = Color.FromArgb(26, 29, 36),
            };
            card.Controls.Add(new Label
            {
                Text = label,
                ForeColor = Color.FromArgb(138, 144, 154),
                Location = new Point(14, 12),
                AutoSize = true,
                Font = new Font("Microsoft YaHei", 10),
            });
            var v = new Label
            {
                Text = "—",
                ForeColor = Color.White,
                Location = new Point(14, 40),
                AutoSize = true,
                Font = new Font("Microsoft YaHei", 16, FontStyle.Bold),
            };
            card.Controls.Add(v);
            card.Controls.Add(new Label
            {
                Text = oid,
                ForeColor = Color.FromArgb(90, 96, 106),
                Location = new Point(14, 74),
                AutoSize = true,
                Font = new Font("Consolas", 8),
            });
            parent.Controls.Add(card);
            y += 110;
            return v;
        }

        // 简易卡（参数设置页用）：标题 + 自定义高度
        private static Panel MakeCard(string title, int width, int height, ref int y)
        {
            var card = new Panel
            {
                Location = new Point(16, y),
                Size = new Size(width, height),
                BackColor = Color.FromArgb(26, 29, 36),
            };
            card.Controls.Add(new Label
            {
                Text = title,
                ForeColor = Color.White,
                Location = new Point(14, 12),
                AutoSize = true,
                Font = new Font("Microsoft YaHei", 12, FontStyle.Bold),
            });
            y += height;
            return card;
        }

        // 小型 Label+Value（在卡片内）
        private static Label AddMini(Panel parent, string label, string val, int top)
        {
            parent.Controls.Add(new Label
            {
                Text = label,
                ForeColor = Color.FromArgb(138, 144, 154),
                Location = new Point(14, top),
                AutoSize = true,
                Font = new Font("Microsoft YaHei", 10),
            });
            var v = new Label
            {
                Text = val,
                ForeColor = Color.White,
                Location = new Point(160, top),
                AutoSize = true,
                Font = new Font("Microsoft YaHei", 11, FontStyle.Bold),
            };
            parent.Controls.Add(v);
            return v;
        }

        // 网络输入字段
        private static TextBox NetField(Panel parent, string label, int x, int y)
        {
            parent.Controls.Add(new Label
            {
                Text = label,
                ForeColor = Color.FromArgb(138, 144, 154),
                Location = new Point(x, y + 6),
                Width = 90,
            });
            var tb = new TextBox
            {
                Location = new Point(x + 96, y),
                Width = 200,
                BackColor = Color.FromArgb(40, 44, 52),
                ForeColor = Color.LightGray,
                BorderStyle = BorderStyle.FixedSingle,
            };
            parent.Controls.Add(tb);
            return tb;
        }

        // ===================== 页面切换 =====================
        private void SwitchPage(string page)
        {
            _currentPage = page;
            pnlSystem.Visible = page == "system";
            pnlDevice.Visible = page == "device";
            pnlHardware.Visible = page == "hardware";
            pnlSensors.Visible = page == "sensors";
            pnlNetwork.Visible = page == "network";
            pnlSettings.Visible = page == "settings";
            if (_running) RefreshCurrent();
        }

        // ===================== 轮询控制 =====================
        private void OnStart(object? s, EventArgs e)
        {
            _snmp.Host = txtHost.Text.Trim();
            if (!int.TryParse(txtPort.Text, out int p)) p = 161;
            _snmp.Port = p;
            _snmp.Community = txtCommunity.Text.Trim();

            _running = true;
            btnStart.Enabled = false; btnStop.Enabled = true;
            SetStatus("● 轮询中", Color.FromArgb(76, 141, 255));
            try
            {
                var ip = _snmp.Get(SnmpEngine.ParseOid(Oids.Network.Ip))?.AsString();
                var mask = _snmp.Get(SnmpEngine.ParseOid(Oids.Network.Mask))?.AsString();
                var gw = _snmp.Get(SnmpEngine.ParseOid(Oids.Network.Gw))?.AsString();
                txtNetIp.Text = ip ?? ""; txtNetMask.Text = mask ?? ""; txtNetGw.Text = gw ?? "";
            }
            catch { }
            int interval = (int)numInterval.Value;
            _timer = new System.Threading.Timer(_ => RefreshCurrent(), null, 0, interval);
        }

        private void OnStop(object? s, EventArgs e)
        {
            _running = false;
            _timer?.Dispose(); _timer = null;
            btnStart.Enabled = true; btnStop.Enabled = false;
            SetStatus("● 空闲", Color.FromArgb(138, 144, 154));
        }

        private void SetStatus(string text, Color c)
        {
            if (lblStatus.InvokeRequired) { lblStatus.Invoke(() => SetStatus(text, c)); return; }
            lblStatus.Text = text; lblStatus.ForeColor = c;
        }

        // ===================== 数据刷新 =====================
        private void RefreshCurrent()
        {
            try
            {
                switch (_currentPage)
                {
                    case "system": RefreshSystem(); break;
                    case "device": RefreshDevice(); break;
                    case "hardware": RefreshHardware(); break;
                    case "sensors": RefreshSensors(); break;
                    case "network": RefreshNetwork(); break;
                    case "settings": RefreshSettings(); break;
                }
                SetStatus("● 已更新 " + DateTime.Now.ToString("HH:mm:ss"), Color.FromArgb(70, 201, 139));
            }
            catch (Exception ex)
            {
                SetStatus("● 错误：" + ex.Message, Color.FromArgb(255, 107, 107));
            }
        }

        private void RefreshSystem()
        {
            var descr = _snmp.Get(SnmpEngine.ParseOid(Oids.System.Descr))?.AsString();
            var tasks = _snmp.Get(SnmpEngine.ParseOid(Oids.System.Tasks))?.AsString();
            var uptime = _snmp.Get(SnmpEngine.ParseOid(Oids.System.Uptime))?.AsString();
            SafeSet(valDescr, descr ?? "—");
            SafeSet(valTasks, tasks ?? "—");
            SafeSet(valUptime, uptime ?? "—");
        }

        private void RefreshDevice()
        {
            var mcu = _snmp.Get(SnmpEngine.ParseOid(Oids.System.Descr))?.AsString();
            var clk = _snmp.Get(SnmpEngine.ParseOid(Oids.System.Clock))?.AsString();
            SafeSet(valDevMcu, mcu ?? "—");
            SafeSet(valDevSysclk, clk ?? "—");
        }

        private void RefreshHardware()
        {
            var mcu = _snmp.Get(SnmpEngine.ParseOid(Oids.System.Descr))?.AsString();
            var clk = _snmp.Get(SnmpEngine.ParseOid(Oids.System.Clock))?.AsString();
            var tasks = _snmp.Get(SnmpEngine.ParseOid(Oids.System.Tasks))?.AsString();
            SafeSet(valMcu, mcu ?? "—");
            SafeSet(valClock, clk ?? "—");
            SafeSet(_hwTasks, tasks ?? "—");
        }

        private void RefreshSensors()
        {
            var map = _snmp.Walk(SnmpEngine.ParseOid(Oids.Root + ".3"));
            for (int i = 0; i < 13; i++)
            {
                var oid = Oids.Sensors.All[i];
                SafeSet(_sensorVals[i], map.TryGetValue(oid, out var v) ? v : "—");
            }
        }

        private void RefreshNetwork()
        {
            var ip = _snmp.Get(SnmpEngine.ParseOid(Oids.Network.Ip))?.AsString();
            var mask = _snmp.Get(SnmpEngine.ParseOid(Oids.Network.Mask))?.AsString();
            var gw = _snmp.Get(SnmpEngine.ParseOid(Oids.Network.Gw))?.AsString();
            var mac = _snmp.Get(SnmpEngine.ParseOid(Oids.Network.Mac))?.AsString();
            SafeSet(valIp, ip ?? "—");
            SafeSet(valMask, mask ?? "—");
            SafeSet(valGw, gw ?? "—");
            SafeSet(valMac, mac ?? "—");
        }

        private void RefreshSettings()
        {
            var led = _snmp.Get(SnmpEngine.ParseOid(Oids.Control.Led))?.AsString();
            var beep = _snmp.Get(SnmpEngine.ParseOid(Oids.Control.Beep))?.AsString();
            var req = _snmp.Get(SnmpEngine.ParseOid(Oids.Stats.Req))?.AsString();
            var err = _snmp.Get(SnmpEngine.ParseOid(Oids.Stats.Err))?.AsString();
            var last = _snmp.Get(SnmpEngine.ParseOid(Oids.Stats.LastUpd))?.AsString();
            var ip = _snmp.Get(SnmpEngine.ParseOid(Oids.Network.Ip))?.AsString();
            var mask = _snmp.Get(SnmpEngine.ParseOid(Oids.Network.Mask))?.AsString();
            var gw = _snmp.Get(SnmpEngine.ParseOid(Oids.Network.Gw))?.AsString();
            var mcu = _snmp.Get(SnmpEngine.ParseOid(Oids.System.Descr))?.AsString();
            var clk = _snmp.Get(SnmpEngine.ParseOid(Oids.System.Clock))?.AsString();
            var sensorMap = _snmp.Walk(SnmpEngine.ParseOid(Oids.Root + ".3"));
            bool ledOn = led == "1" || led == "ON";
            bool beepOn = beep == "1" || beep == "ON";
            InvokeIfNeeded(() =>
            {
                tgLed.State = ledOn; valLed.Text = "状态：" + (ledOn ? "ON" : "OFF");
                tgBeep.State = beepOn; valBeep.Text = "状态：" + (beepOn ? "ON" : "OFF");
                if (!txtNetIp.Focused) txtNetIp.Text = ip ?? "";
                if (!txtNetMask.Focused) txtNetMask.Text = mask ?? "";
                if (!txtNetGw.Focused) txtNetGw.Text = gw ?? "";
                valSetMcu.Text = mcu ?? "—";
                valSetClock.Text = clk ?? "—";
                for (int i = 0; i < 13; i++)
                {
                    var oid = Oids.Sensors.All[i];
                    _setSensorVals[i].Text = sensorMap.TryGetValue(oid, out var v) ? v : "—";
                }
            });
            SafeSet(valReq, req ?? "—");
            SafeSet(valErr, err ?? "—");
            SafeSet(valLastUpd, last ?? "—");
        }

        private void InvokeIfNeeded(Action a)
        {
            if (InvokeRequired) Invoke(a); else a();
        }

        // ===================== 控制（Set） =====================
        private void OnToggle(ToggleSwitch tg, string oid, Label status)
        {
            if (!_running) { MessageBox.Show("请先点击\"启动轮询\"再下发控制。", "提示"); return; }
            bool next = !tg.State;
            bool ok = _snmp.SetInt(SnmpEngine.ParseOid(oid), next ? 1 : 0);
            if (ok)
            {
                tg.State = next;
                status.Text = "状态：" + (next ? "ON" : "OFF");
                RefreshSettings();
            }
            else
            {
                MessageBox.Show($"Set {oid} 失败（设备无响应或拒绝）。", "错误");
            }
        }

        private void OnApplyNet(object? s, EventArgs e)
        {
            if (!_running) { MessageBox.Show("请先点击\"启动轮询\"再下发设置。", "提示"); return; }
            var ip = txtNetIp.Text.Trim();
            var mask = txtNetMask.Text.Trim();
            var gw = txtNetGw.Text.Trim();
            if (!IsValidIp(ip) || !IsValidIp(mask) || !IsValidIp(gw))
            {
                MessageBox.Show("IP / 掩码 / 网关 格式不合法（应为 x.x.x.x）。", "输入错误");
                return;
            }
            bool ok = _snmp.SetString(SnmpEngine.ParseOid(Oids.Network.Ip), ip)
                    & _snmp.SetString(SnmpEngine.ParseOid(Oids.Network.Mask), mask)
                    & _snmp.SetString(SnmpEngine.ParseOid(Oids.Network.Gw), gw);
            if (ok)
                MessageBox.Show("网络参数已写入设备（EEPROM）。\n重新上电或点击\"系统复位\"后生效。", "成功");
            else
                MessageBox.Show("网络参数写入失败（设备无响应或拒绝）。", "错误");
        }

        private void OnReset(object? s, EventArgs e)
        {
            if (!_running) { MessageBox.Show("请先点击\"启动轮询\"再执行复位。", "提示"); return; }
            var r = MessageBox.Show("确定要执行系统软复位吗？\n设备将立刻重启。", "系统复位", MessageBoxButtons.OKCancel, MessageBoxIcon.Warning);
            if (r != DialogResult.OK) return;
            bool ok = _snmp.ResetDevice();
            if (!ok) MessageBox.Show("复位指令发送失败（设备无响应）。", "错误");
        }

        private static bool IsValidIp(string s)
        {
            var p = s.Split('.');
            if (p.Length != 4) return false;
            foreach (var x in p) if (!byte.TryParse(x, out _)) return false;
            return true;
        }

        private void SafeSet(Label lbl, string text)
        {
            if (lbl.InvokeRequired) { lbl.Invoke(() => lbl.Text = text); }
            else lbl.Text = text;
        }
    }
}
