using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Drawing;
using System.IO.Ports;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace NesPadTool;

/// <summary>
/// NES 虚拟手柄工具主窗口。
/// - 串口：复用串口调试助手的 SerialPort 连接 / 扫描 / UTF-8 收发模式。
/// - 手柄：方向键 + A/B + Select/Start，统一风格（StyleButton 思路），鼠标与物理键共用同一高亮态 → 图标显示一致。
/// - 映射：物理键 → NES 虚拟键，固定不可改，配置不再保存映射。
/// - 配置：串口 port/baud + 皮肤 存于程序目录 nespad.config.json（参考记账工具 AppConfig）。
/// </summary>
public class MainForm : Form
{
    // ---- 串口 ----
    private readonly SerialPort _port = new SerialPort();
    private readonly System.Windows.Forms.Timer _scanTimer = new System.Windows.Forms.Timer();
    private readonly Encoding _enc = Encoding.UTF8;
    private readonly Decoder _decoder = Encoding.UTF8.GetDecoder();
    private readonly object _portLock = new object();
    private readonly ConcurrentQueue<byte> _recvQueue = new();   // 接收缓冲：后台线程入队，UI 定时批量取
    private readonly System.Windows.Forms.Timer _recvTimer = new(); // 接收刷新节流定时器
    private long _recvBytes;
    private bool _closing;
    private const int LogCap = 8192;  // 日志文本上限（字符），防止高速数据下 UI 文本无限增长拖慢界面

    // ---- 配置 / 皮肤 ----
    private Skin _skin = SkinPresets.Skins[0];
    private readonly Dictionary<string, string> _keyMap = KeyMap.Default(); // 物理键 -> NES 虚拟键（固定）
    private readonly HashSet<string> _heldPhysical = new();               // 当前按下的物理键（防 auto-repeat）
    private string _mouseHeld = "";                                       // 鼠标按住中的 NES 键
    private bool _ready;                                                  // 加载完成后才允许配置落盘

    // ---- 控件引用 ----
    private ComboBox _cboPort = null!;
    private ComboBox _cboBaud = null!;
    private Button _btnOpen = null!;
    private Panel _topBar = null!;
    private Label _lblTitle = null!;
    private ComboBox _cboSkin = null!;
    private TextBox _txtLog = null!;
    private ToolStripStatusLabel _lblPort = null!;
    private ToolStripStatusLabel _lblRecvStat = null!;
    private ToolStripStatusLabel _lblHint = null!;

    private readonly List<Button> _padButtons = new();
    private readonly Dictionary<Button, string> _btnRole = new();     // 命令按钮角色（用于统一配色）
    private readonly Dictionary<string, TextBox> _mapTextBox = new(); // NES 键 -> 映射显示框
    private readonly StringBuilder _recvText = new();

    private const int PadSize = 58;   // 方向键 / A / B 按键边长
    private const int MiniW = 74;      // SELECT / START 宽度
    private const int MiniH = 42;      // SELECT / START 高度
    private const int Gap = 14;        // 单元格内边距（按钮与单元格间距）
    private const int Spacer = 36;     // 方向键与 A/B 两组之间的留白

    public MainForm()
    {
        InitializeComponent();
        LoadConfigIntoUi();
        InitPort();
        RefreshPorts();
        _ready = true;
    }

    #region 初始化

    private void InitializeComponent()
    {
        Text = "NES 虚拟手柄工具";
        ClientSize = new Size(1180, 680);
        Font = new Font("Microsoft YaHei", 9F);
        FormBorderStyle = FormBorderStyle.FixedSingle;
        MaximizeBox = true;
        MinimizeBox = true;
        StartPosition = FormStartPosition.CenterScreen;
        KeyPreview = true; // 表单优先接收按键，便于统一映射物理键

        // ===== 顶栏 =====
        _topBar = new Panel { Dock = DockStyle.Top, Height = 46 };
        _lblTitle = new Label
        {
            Text = "NES 虚拟手柄工具",
            ForeColor = Color.White,
            Font = new Font(Font, FontStyle.Bold),
            AutoSize = true,
            Location = new Point(12, 13)
        };
        _cboSkin = new ComboBox
        {
            DropDownStyle = ComboBoxStyle.DropDownList,
            Width = 150,
            Height = 26,
            Anchor = AnchorStyles.Top | AnchorStyles.Right
        };
        _cboSkin.Items.AddRange(SkinPresets.Skins.ConvertAll(s => (object)s.Name).ToArray());
        _cboSkin.SelectedIndexChanged += (_, _) => ApplySkin(SkinPresets.Skins[_cboSkin.SelectedIndex]);
        _topBar.Controls.Add(_lblTitle);
        _topBar.Controls.Add(_cboSkin);

        // ===== 内容区：左控制栏 + 右手柄/日志 =====
        var content = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 2,
            RowCount = 1,
            Padding = new Padding(6)
        };
        content.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 28));
        content.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 72));
        content.Controls.Add(BuildLeftPanel(), 0, 0);
        content.Controls.Add(BuildRightPanel(), 1, 0);

        // ===== 状态栏 =====
        var ss = new StatusStrip { Dock = DockStyle.Bottom };
        _lblPort = new ToolStripStatusLabel("未打开") { BorderSides = ToolStripStatusLabelBorderSides.Right };
        _lblRecvStat = new ToolStripStatusLabel("接收: 0 字节") { BorderSides = ToolStripStatusLabelBorderSides.Right };
        _lblHint = new ToolStripStatusLabel("就绪") { Spring = true, Alignment = ToolStripItemAlignment.Right };
        ss.Items.AddRange(new ToolStripItem[] { _lblPort, _lblRecvStat, _lblHint });

        Controls.Add(ss);
        Controls.Add(content);
        Controls.Add(_topBar);

        // ===== 事件 =====
        _btnOpen.Click += BtnOpen_Click;
        _port.DataReceived += Port_DataReceived;
        KeyDown += Form_KeyDown;
        KeyUp += Form_KeyUp;
        FormClosing += MainForm_FormClosing;
        FormClosed += (_, _) => { try { _port.Dispose(); } catch { } };

        _scanTimer.Interval = 1500;
        _scanTimer.Tick += ScanTimer_Tick;
        _scanTimer.Start();

        // 接收刷新节流：DataReceived 只把字节入队，UI 定时(100ms)批量取一次，避免高频 BeginInvoke 堆积导致卡死
        _recvTimer.Interval = 100;
        _recvTimer.Tick += RecvTimer_Tick;
        _recvTimer.Start();

        ApplySkin(_skin); // 初始化配色 + 顶栏
    }

    // ===== 左控制栏 =====
    private Control BuildLeftPanel()
    {
        var host = new FlowLayoutPanel
        {
            Dock = DockStyle.Fill,
            FlowDirection = FlowDirection.TopDown,
            WrapContents = false,
            AutoScroll = true,
            Padding = new Padding(2)
        };
        host.Controls.Add(BuildSerialPanel());
        host.Controls.Add(BuildQuickPanel());
        host.Controls.Add(BuildMappingPanel());
        return host;
    }

    private Panel BuildSerialPanel()
    {
        var p = new Panel { Width = 278, Height = 158, BorderStyle = BorderStyle.FixedSingle, Padding = new Padding(6) };
        var title = new Label { Text = "串口配置", Dock = DockStyle.Top, Height = 22, Font = new Font(Font, FontStyle.Bold) };

        var grid = new TableLayoutPanel { Dock = DockStyle.Top, ColumnCount = 2, AutoSize = true, Padding = new Padding(2) };
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        _cboPort = MakeCombo(Array.Empty<object>(), "COM1");
        _cboBaud = MakeCombo(new object[] { 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600 }, "115200");
        AddRow(grid, "串口号:", _cboPort);
        AddRow(grid, "波特率:", _cboBaud);

        _btnOpen = new Button
        {
            Dock = DockStyle.Bottom,
            Height = 38,
            Text = "打开串口",
            Font = new Font(Font, FontStyle.Bold)
        };

        p.Controls.Add(_btnOpen);
        p.Controls.Add(grid);
        p.Controls.Add(title);
        return p;
    }

    private Panel BuildQuickPanel()
    {
        var p = new Panel { Width = 278, Height = 196, BorderStyle = BorderStyle.FixedSingle, Padding = new Padding(6) };
        var title = new Label { Text = "快捷指令", Dock = DockStyle.Top, Height = 22, Font = new Font(Font, FontStyle.Bold) };

        var grid = new TableLayoutPanel { Dock = DockStyle.Top, ColumnCount = 2, AutoSize = true, Padding = new Padding(2) };
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));

        int cmdIdx = 0;
        void AddCmd(string text, string role, string cmd)
        {
            var b = new Button { Text = text, Height = 30, Margin = new Padding(2) };
            b.Click += (_, _) => { SendLine(cmd); this.ActiveControl = null; };
            StyleButton(b, role);
            int col = cmdIdx % 2, row = cmdIdx / 2; cmdIdx++;
            if (grid.RowCount < row + 1) { grid.RowCount = row + 1; grid.RowStyles.Add(new RowStyle(SizeType.AutoSize)); }
            grid.Controls.Add(b, col, row);
        }
        AddCmd("打开 NES 页", "secondary", NesKeys.Cmd.OpenNes);
        AddCmd("ROM 列表", "neutral", NesKeys.Cmd.RomList);
        AddCmd("加载 ROM0", "primary", NesKeys.Cmd.RomLoad(0));
        AddCmd("停止模拟", "neutral", NesKeys.Cmd.RomStop);
        AddCmd("状态", "neutral", NesKeys.Cmd.Status);
        AddCmd("释放全部", "neutral", NesKeys.Release());

        p.Controls.Add(grid);
        p.Controls.Add(title);
        return p;
    }

    private Panel BuildMappingPanel()
    {
        var p = new Panel { Width = 278, Height = 300, BorderStyle = BorderStyle.FixedSingle, Padding = new Padding(6) };
        var title = new Label { Text = "按键映射（物理键 → NES）", Dock = DockStyle.Top, Height = 22, Font = new Font(Font, FontStyle.Bold) };

        var grid = new TableLayoutPanel { ColumnCount = 2, AutoSize = true, Padding = new Padding(2) };
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 55));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 45));

        foreach (var (key, label) in NesKeys.Buttons)
        {
            int r = grid.RowCount; grid.RowCount++; grid.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            var lbl = new Label { Text = $"{label} ({key})", AutoSize = true, Anchor = AnchorStyles.Left, Margin = new Padding(2, 6, 2, 2) };
            var tb = new TextBox { Width = 78, ReadOnly = true, BackColor = _skin.InputBg, Margin = new Padding(0, 2, 2, 2) };
            _mapTextBox[key] = tb;
            grid.Controls.Add(lbl, 0, r);
            grid.Controls.Add(tb, 1, r);
        }

        var hint = new Label
        {
            Dock = DockStyle.Bottom,
            Height = 28,
            Text = "映射已固定，不可修改",
            ForeColor = _skin.SubText,
            TextAlign = ContentAlignment.MiddleCenter
        };

        p.Controls.Add(hint);
        p.Controls.Add(grid);
        p.Controls.Add(title);
        return p;
    }

    // ===== 右：手柄 + 日志 =====
    private Control BuildRightPanel()
    {
        var tlp = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 2, Padding = new Padding(6) };
        tlp.RowStyles.Add(new RowStyle(SizeType.Percent, 78));
        tlp.RowStyles.Add(new RowStyle(SizeType.Percent, 22));
        tlp.Controls.Add(BuildKeypadPanel(), 0, 0);
        tlp.Controls.Add(BuildLogPanel(), 0, 1);
        return tlp;
    }

    private Panel BuildKeypadPanel()
    {
        var p = new Panel { Dock = DockStyle.Fill, BorderStyle = BorderStyle.FixedSingle, Padding = new Padding(6) };
        var title = new Label { Text = "NES 虚拟手柄（鼠标点按 / 键盘映射均可）", Dock = DockStyle.Top, Height = 22, Font = new Font(Font, FontStyle.Bold) };

        // 居中容器：留出足够边距，避免右侧按键被裁切
        var area = new Panel { Dock = DockStyle.Fill, Padding = new Padding(8), AutoScroll = false };

        var dpad = BuildDpad();
        var abStart = BuildAbStart();

        // 固定尺寸的三列：方向键 | 留白 | A/B/SELECT/START，列宽严格等于内部网格宽度
        var kp = new TableLayoutPanel
        {
            AutoSize = true,
            Anchor = AnchorStyles.None,
            ColumnCount = 3,
            RowCount = 1,
            Padding = new Padding(14)
        };
        kp.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, dpad.Width));
        kp.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, Spacer)); // 两组之间的留白
        kp.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, abStart.Width));
        kp.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        kp.Controls.Add(dpad, 0, 0);
        kp.Controls.Add(abStart, 2, 0);

        // 将 kp 放在 area 中央
        void ReCenter() => kp.Location = new Point(
            Math.Max(0, (area.ClientSize.Width - kp.Width) / 2),
            Math.Max(0, (area.ClientSize.Height - kp.Height) / 2));
        ReCenter();
        area.Controls.Add(kp);
        area.Resize += (_, _) => ReCenter();

        p.Controls.Add(area);
        p.Controls.Add(title);
        return p;
    }

    private Control BuildDpad()
    {
        int cell = PadSize + Gap; // 单元格尺寸 = 按键尺寸 + 间距，与按钮严格匹配
        var g = new TableLayoutPanel
        {
            ColumnCount = 3,
            RowCount = 3,
            BackColor = Color.Transparent,
            Padding = new Padding(0),
            Size = new Size(cell * 3, cell * 3) // 固定尺寸，杜绝与所在区域不匹配
        };
        for (int i = 0; i < 3; i++)
        {
            g.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, cell));
            g.RowStyles.Add(new RowStyle(SizeType.Absolute, cell));
        }

        g.Controls.Add(MakePadButton("up", "↑"), 1, 0);
        g.Controls.Add(MakePadButton("left", "←"), 0, 1);
        var center = new Label
        {
            Text = "NES",
            AutoSize = false,
            Dock = DockStyle.Fill,
            TextAlign = ContentAlignment.MiddleCenter,
            Font = new Font(Font, FontStyle.Bold)
        };
        g.Controls.Add(center, 1, 1);
        g.Controls.Add(MakePadButton("right", "→"), 2, 1);
        g.Controls.Add(MakePadButton("down", "↓"), 1, 2);
        return g;
    }

    private Control BuildAbStart()
    {
        int cellW = Math.Max(PadSize, MiniW) + Gap; // 列宽取较宽者，保证 SELECT/START 不被裁切
        int cellH = PadSize + Gap;
        int cellMH = MiniH + Gap;
        int w = cellW * 2;
        int h = cellH + cellMH;
        var g = new TableLayoutPanel
        {
            ColumnCount = 2,
            RowCount = 2,
            BackColor = Color.Transparent,
            Padding = new Padding(0),
            Size = new Size(w, h) // 固定尺寸
        };
        g.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, cellW));
        g.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, cellW));
        g.RowStyles.Add(new RowStyle(SizeType.Absolute, cellH));
        g.RowStyles.Add(new RowStyle(SizeType.Absolute, cellMH));
        g.Controls.Add(MakePadButton("b", "B"), 0, 0);
        g.Controls.Add(MakePadButton("a", "A"), 1, 0);
        g.Controls.Add(MakePadButton("select", "SELECT", MiniW, MiniH), 0, 1);
        g.Controls.Add(MakePadButton("start", "START", MiniW, MiniH), 1, 1);
        return g;
    }

    private Panel BuildLogPanel()
    {
        var p = new Panel { Dock = DockStyle.Fill, BorderStyle = BorderStyle.FixedSingle, Padding = new Padding(6) };
        var title = new Label { Text = "设备回显（OK/ERR）", Dock = DockStyle.Top, Height = 22, Font = new Font(Font, FontStyle.Bold) };
        _txtLog = new TextBox
        {
            Dock = DockStyle.Fill,
            Multiline = true,
            ScrollBars = ScrollBars.Vertical,
            ReadOnly = true,
            Font = new Font("Consolas", 10F),
            WordWrap = false,
            BackColor = Color.White
        };
        p.Controls.Add(_txtLog);
        p.Controls.Add(title);
        return p;
    }

    private Button MakePadButton(string nesKey, string label, int w = PadSize, int h = PadSize)
    {
        var b = new Button
        {
            Text = label,
            Tag = nesKey,
            Dock = DockStyle.Fill, // 填满所在单元格，与单元格尺寸严格匹配
            Font = new Font("Microsoft YaHei", label.Length > 1 ? 8.5F : 13F, FontStyle.Bold),
            FlatStyle = FlatStyle.Flat,
            Margin = new Padding(Gap / 2),
            TabStop = false
        };
        b.FlatAppearance.BorderSize = 2;
        b.MouseDown += (_, _) => PadDown(nesKey);
        b.MouseUp += (_, _) => PadUp(nesKey);
        b.MouseLeave += (_, _) => { if (_mouseHeld == nesKey) PadUp(nesKey); };
        _padButtons.Add(b);
        return b;
    }

    #endregion

    #region 统一风格（参考记账工具 StyleButton / ApplySkin）

    private void StyleButton(Button b, string role)
    {
        _btnRole[b] = role;
        RecolorButton(b);
    }

    private void RecolorButton(Button b)
    {
        if (!_btnRole.TryGetValue(b, out var role)) return;
        (b.BackColor, b.ForeColor) = role switch
        {
            "primary" => (_skin.Primary, Color.White),
            "secondary" => (_skin.Secondary, Color.White),
            "success" => (_skin.Success, Color.White),
            _ => (_skin.Neutral, Color.White)
        };
        b.FlatStyle = FlatStyle.Flat;
        b.FlatAppearance.BorderSize = 0;
    }

    private void RecolorAll()
    {
        foreach (var b in _btnRole.Keys) RecolorButton(b);
        foreach (var b in _padButtons) SetPadFaceNormal(b);
        _topBar.BackColor = _skin.Primary;
        _lblTitle.ForeColor = Color.White;
    }

    private void SetPadFaceNormal(Button b)
    {
        b.BackColor = _skin.Panel;
        b.ForeColor = _skin.Text;
        b.FlatAppearance.BorderColor = _skin.Secondary;
    }

    private void ApplySkin(Skin s)
    {
        _skin = s;
        RecolorAll();
        // 同步皮肤下拉选择
        int idx = SkinPresets.Skins.IndexOf(s);
        if (idx >= 0 && _cboSkin.SelectedIndex != idx) _cboSkin.SelectedIndex = idx;
        if (_ready) SaveConfig();
    }

    #endregion

    #region 手柄按键（鼠标 + 物理键共用同一高亮态）

    private void PadDown(string nesKey)
    {
        SendLine(NesKeys.Down(nesKey));
        SetKeyPressed(nesKey, true);
        _mouseHeld = nesKey;
    }

    private void PadUp(string nesKey)
    {
        SendLine(NesKeys.Up(nesKey));
        SetKeyPressed(nesKey, false);
        _mouseHeld = "";
    }

    /// <summary>统一的高亮入口：鼠标与物理键都走这里，保证图标显示一致。</summary>
    private void SetKeyPressed(string nesKey, bool pressed)
    {
        foreach (var b in _padButtons)
        {
            if (b.Tag is not string tag || tag != nesKey) continue;
            if (pressed)
            {
                b.BackColor = _skin.Secondary;
                b.ForeColor = Color.White;
                b.FlatAppearance.BorderColor = _skin.Primary;
            }
            else
            {
                SetPadFaceNormal(b);
            }
        }
    }

    private void Form_KeyDown(object? sender, KeyEventArgs e)
    {
        // 无论焦点在哪个控件（包括刚点过的按钮），都拦截映射的物理键 W/S/A/D/Q/E/J/K。
        // 这些键不属于按钮的“激活键”(Enter/Space)，拦截后不会误触按钮；
        // 因此不能因“焦点在按钮上”而早退——否则点过按钮后键盘映射会完全失效。
        var pk = e.KeyCode.ToString();
        if (_keyMap.TryGetValue(pk, out var nes))
        {
            if (_heldPhysical.Add(pk)) // 仅首次按下发送 down，屏蔽系统 auto-repeat
            {
                SendLine(NesKeys.Down(nes));
                SetKeyPressed(nes, true);
            }
            // 仅标记 Handled 阻止焦点控件响应按键即可；
            // 切勿设置 SuppressKeyPress —— 它会连带吞掉 KeyUp，导致按键卡死在按下状态。
            e.Handled = true;
        }
    }

    private void Form_KeyUp(object? sender, KeyEventArgs e)
    {
        var pk = e.KeyCode.ToString();
        if (_keyMap.TryGetValue(pk, out var nes) && _heldPhysical.Remove(pk))
        {
            SendLine(NesKeys.Up(nes));
            SetKeyPressed(nes, false);
            e.Handled = true;
        }
    }

    #endregion

    #region 映射显示（固定，不可学习）

    private void RefreshMappingUI()
    {
        foreach (var (key, _) in NesKeys.Buttons)
        {
            string? bound = null;
            foreach (var kv in _keyMap) if (kv.Value == key) { bound = kv.Key; break; }
            if (_mapTextBox.TryGetValue(key, out var tb))
                tb.Text = bound ?? "(未绑定)";
        }
    }

    #endregion

    #region 串口（复用串口调试助手模式）

    private void MainForm_FormClosing(object? sender, FormClosingEventArgs e)
    {
        _closing = true;
        try { _scanTimer?.Stop(); } catch { }
        try { _recvTimer?.Stop(); } catch { }
        // 先移除事件，避免 Close 期间异步回调触发
        try { _port.DataReceived -= Port_DataReceived; } catch { }
        // 后台线程关闭：SerialPort.Close 可能等待接收线程结束而阻塞，放后台避免 UI 卡死
        try
        {
            var port = _port;
            Task.Run(() =>
            {
                try { lock (_portLock) { if (port.IsOpen) port.Close(); } }
                catch { }
            });
        }
        catch { }
        SaveConfig();
    }

    private void InitPort()
    {
        _port.RtsEnable = true;
        _port.DtrEnable = true;
        _port.ReadTimeout = 500;
        _port.WriteTimeout = 500;
    }

    private void RefreshPorts()
    {
        if (_closing) return;
        string[] ports;
        try { ports = SerialPort.GetPortNames(); }
        catch { ports = Array.Empty<string>(); }

        string cur = _cboPort.Text;
        _cboPort.Items.Clear();
        if (ports.Length > 0) _cboPort.Items.AddRange(ports);
        if (Array.IndexOf(ports, cur) >= 0) _cboPort.Text = cur;
        else if (ports.Length > 0 && !_port.IsOpen) _cboPort.SelectedIndex = 0;
    }

    private void ScanTimer_Tick(object? sender, EventArgs e)
    {
        // 已打开时不再枚举端口：GetPortNames 在部分驱动/下位机不稳定时会很慢甚至卡死，且与接收线程竞争。
        // 端口意外断开由接收异常/发送异常感知，用户手动关闭即可，避免无谓枚举导致死机。
        if (_closing || _port.IsOpen) return;
        string[] ports;
        try { ports = SerialPort.GetPortNames(); }
        catch { return; }

        try
        {
            string cur = _cboPort.Text;
            _cboPort.Items.Clear();
            if (ports.Length > 0) _cboPort.Items.AddRange(ports);
            if (Array.IndexOf(ports, cur) >= 0) _cboPort.Text = cur;
            else if (ports.Length > 0) _cboPort.SelectedIndex = 0;
        }
        catch
        {
            // 窗体正在关闭或枚举异常时忽略
        }
    }

    private void BtnOpen_Click(object? sender, EventArgs e)
    {
        if (!_port.IsOpen)
        {
            if (_cboPort.Items.Count == 0 || string.IsNullOrEmpty(_cboPort.Text))
            {
                MessageBox.Show("未检测到串口，请检查设备连接。", "提示", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                RefreshPorts();
                return;
            }
            try
            {
                lock (_portLock)
                {
                    _port.PortName = _cboPort.Text;
                    _port.BaudRate = int.Parse(_cboBaud.Text);
                    _port.Open();
                }
                UpdateOpenState(true);
                SaveConfig();
            }
            catch (Exception ex)
            {
                MessageBox.Show("打开串口失败：\n" + ex.Message, "错误", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }
        else
        {
            lock (_portLock) { try { _port.Close(); } catch { } }
            UpdateOpenState(false);
        }
        // 点击后清除焦点，避免焦点一直黏在按钮上导致键盘映射失效
        this.ActiveControl = null;
    }

    private void UpdateOpenState(bool open)
    {
        _btnOpen.Text = open ? "关闭串口" : "打开串口";
        _btnOpen.BackColor = open ? Color.LightCoral : Color.LightGreen;
        _cboPort.Enabled = !open;
        _cboBaud.Enabled = !open;
        _lblPort.Text = open ? $"已打开  {_port.PortName}  {_port.BaudRate}" : "未打开";
    }

    private void Port_DataReceived(object sender, SerialDataReceivedEventArgs e)
    {
        if (_closing) return;
        try
        {
            byte[] data;
            lock (_portLock)
            {
                if (!_port.IsOpen) return;
                int n = _port.BytesToRead;
                if (n <= 0) return;
                data = new byte[n];
                int r = _port.Read(data, 0, n);
                if (r <= 0) return;
                if (r < data.Length) Array.Resize(ref data, r);
            }
            // 只入队，不在此处做 UI 操作，避免高频 BeginInvoke 堆积导致 UI 卡死
            foreach (var b in data) _recvQueue.Enqueue(b);
        }
        catch
        {
            // 串口不稳定时忽略接收异常，避免崩溃
        }
    }

    // 节流刷新：每 100ms 从队列批量取一次，最多 4096 字节，UI 更新频率恒定，杜绝堆积
    private void RecvTimer_Tick(object? sender, EventArgs e)
    {
        if (_closing || IsDisposed) return;

        // 背压：数据速率超消费能力时丢弃最旧数据，避免队列无限增长耗尽内存（极端高速场景）
        const int maxQueue = 1 << 20; // 1MB 高水位
        if (_recvQueue.Count > maxQueue)
        {
            int drop = _recvQueue.Count - (maxQueue >> 1);
            for (int i = 0; i < drop; i++) _recvQueue.TryDequeue(out _);
        }
        if (_recvQueue.IsEmpty) return;

        const int maxOnce = 4096;
        byte[] buf = new byte[Math.Min(maxOnce, _recvQueue.Count)];
        int taken = 0;
        while (taken < buf.Length && _recvQueue.TryDequeue(out byte b)) buf[taken++] = b;

        _recvBytes += taken;
        _lblRecvStat.Text = $"接收: {_recvBytes} 字节";
        char[] ch = new char[_decoder.GetCharCount(buf, 0, taken)];
        int n = _decoder.GetChars(buf, 0, taken, ch, 0);
        _recvText.Append(ch, 0, n);
        if (_recvText.Length > LogCap) _recvText.Remove(0, _recvText.Length - LogCap);
        _txtLog.Text = _recvText.ToString();
        _txtLog.SelectionStart = _txtLog.TextLength;
        _txtLog.ScrollToCaret();
    }

    private void SendLine(string line)
    {
        if (_closing) return;
        if (!_port.IsOpen)
        {
            Hint("! 串口未打开");
            return;
        }
        // 后台线程发送：下位机不读取时 Write 最久阻塞 WriteTimeout，放后台避免 UI 卡死
        var bytes = _enc.GetBytes(line + "\r\n");
        Task.Run(() =>
        {
            try
            {
                lock (_portLock)
                {
                    if (!_port.IsOpen) return;
                    _port.Write(bytes, 0, bytes.Length);
                }
            }
            catch (Exception ex)
            {
                try { this.Invoke(new Action(() => Hint("! 发送失败: " + ex.Message))); } catch { }
            }
        });
    }

    #endregion

    #region 配置

    private void LoadConfigIntoUi()
    {
        var cfg = AppConfig.Load();
        if (!string.IsNullOrEmpty(cfg.Port)) _cboPort.Text = cfg.Port;
        if (cfg.Baud > 0) _cboBaud.Text = cfg.Baud.ToString();
        if (!string.IsNullOrEmpty(cfg.SkinName))
        {
            var s = SkinPresets.Skins.Find(x => x.Name == cfg.SkinName);
            if (s != null) _skin = s;
        }
        // 映射固定，不读取配置文件中的 KeyMap
        RefreshMappingUI();
        ApplySkin(_skin); // 应用已保存皮肤配色
    }

    private void SaveConfig()
    {
        AppConfig.Save(new AppConfigData
        {
            Port = _cboPort.Text,
            Baud = int.TryParse(_cboBaud.Text, out var b) ? b : 115200,
            SkinName = _skin.Name,
            KeyMap = null // 映射固定，不再持久化
        });
    }

    #endregion

    #region 工具方法

    private void Hint(string s) => _lblHint.Text = s;

    private static ComboBox MakeCombo(object[] items, string def)
    {
        var c = new ComboBox
        {
            DropDownStyle = ComboBoxStyle.DropDownList,
            Anchor = AnchorStyles.Left | AnchorStyles.Right | AnchorStyles.Top,
            Width = 120
        };
        if (items.Length > 0) c.Items.AddRange(items);
        c.Text = def;
        return c;
    }

    private static void AddRow(TableLayoutPanel g, string label, Control ctrl)
    {
        int r = g.RowCount; g.RowCount++; g.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        g.Controls.Add(new Label { Text = label, AutoSize = true, Anchor = AnchorStyles.Left, Margin = new Padding(2, 7, 2, 2) }, 0, r);
        g.Controls.Add(ctrl, 1, r);
    }

    #endregion
}
