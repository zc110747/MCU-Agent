using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO.Ports;
using System.Text;
using System.Windows.Forms;

namespace NesPadTool;

/// <summary>
/// NES 虚拟手柄工具主窗口。
/// - 串口：复用串口调试助手的 SerialPort 连接 / 扫描 / UTF-8 收发模式。
/// - 手柄：方向键 + A/B + Select/Start，统一风格（StyleButton 思路），鼠标与物理键共用同一高亮态 → 图标显示一致。
/// - 映射：物理键 → NES 虚拟键，可「学习」重绑，配置落盘。
/// - 配置：串口 port/baud + 皮肤 + 映射 存于程序目录 nespad.config.json（参考记账工具 AppConfig）。
/// </summary>
public class MainForm : Form
{
    // ---- 串口 ----
    private readonly SerialPort _port = new SerialPort();
    private readonly System.Windows.Forms.Timer _scanTimer = new System.Windows.Forms.Timer();
    private readonly Encoding _enc = Encoding.UTF8;
    private readonly Decoder _decoder = Encoding.UTF8.GetDecoder();
    private long _recvBytes;

    // ---- 配置 / 皮肤 ----
    private Skin _skin = SkinPresets.Skins[0];
    private Dictionary<string, string> _keyMap = KeyMap.Default(); // 物理键 -> NES 虚拟键
    private readonly HashSet<string> _heldPhysical = new();         // 当前按下的物理键（防 auto-repeat）
    private string? _learningNesKey;                               // 正在学习的 NES 键
    private string _mouseHeld = "";                                // 鼠标按住中的 NES 键
    private bool _ready;                                           // 加载完成后才允许配置落盘

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

    private const int PadSize = 52;   // 方向键 / A / B 按键边长
    private const int MiniW = 72;      // SELECT / START 宽度
    private const int MiniH = 40;      // SELECT / START 高度
    private const int Gap = 8;         // 单元格内边距（按钮与单元格间距）

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
        ClientSize = new Size(1020, 660);
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
        content.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 35));
        content.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 65));
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
        FormClosing += (_, _) => { try { _port?.Close(); } catch { } SaveConfig(); };

        _scanTimer.Interval = 1500;
        _scanTimer.Tick += ScanTimer_Tick;
        _scanTimer.Start();

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
        var p = new Panel { Width = 278, Height = 372, BorderStyle = BorderStyle.FixedSingle, Padding = new Padding(6) };
        var title = new Label { Text = "按键映射（物理键 → NES）", Dock = DockStyle.Top, Height = 22, Font = new Font(Font, FontStyle.Bold) };

        var grid = new TableLayoutPanel { ColumnCount = 3, AutoSize = true, Padding = new Padding(2) };
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 42));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 35));
        grid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 23));

        foreach (var (key, label) in NesKeys.Buttons)
        {
            int r = grid.RowCount; grid.RowCount++; grid.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            var lbl = new Label { Text = $"{label} ({key})", AutoSize = true, Anchor = AnchorStyles.Left, Margin = new Padding(2, 6, 2, 2) };
            var tb = new TextBox { Width = 78, ReadOnly = true, BackColor = _skin.InputBg, Margin = new Padding(0, 2, 2, 2) };
            _mapTextBox[key] = tb;
            var learn = new Button { Text = "学习", Width = 50, Height = 24, Margin = new Padding(2) };
            learn.Click += (_, _) => StartLearn(key, label);
            grid.Controls.Add(lbl, 0, r);
            grid.Controls.Add(tb, 1, r);
            grid.Controls.Add(learn, 2, r);
        }

        var btnReset = new Button
        {
            Dock = DockStyle.Bottom,
            Height = 28,
            Text = "恢复默认映射",
            Margin = new Padding(0, 4, 0, 0)
        };
        btnReset.Click += (_, _) =>
        {
            _keyMap = KeyMap.Default();
            RefreshMappingUI();
            SaveConfig();
            Hint("已恢复默认映射");
            this.ActiveControl = null;
        };

        p.Controls.Add(btnReset);
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

        // 标题下方的居中区域：单格 Percent 容器，内部手柄 Anchor=None 即居中，避免被拉伸
        var area = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 1, Padding = new Padding(0) };
        area.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        area.RowStyles.Add(new RowStyle(SizeType.Percent, 100));

        var dpad = BuildDpad();
        var abStart = BuildAbStart();

        // 固定尺寸的左右两列，列宽严格等于内部网格宽度，杜绝尺寸失配
        var kp = new TableLayoutPanel
        {
            AutoSize = true,
            Anchor = AnchorStyles.None,
            ColumnCount = 2,
            RowCount = 1,
            Padding = new Padding(10)
        };
        kp.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, dpad.Width));
        kp.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, abStart.Width));
        kp.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        kp.Controls.Add(dpad, 0, 0);
        kp.Controls.Add(abStart, 1, 0);

        area.Controls.Add(kp, 0, 0);
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
        // 学习模式优先捕获任意物理键
        if (_learningNesKey != null)
        {
            if (e.KeyCode == Keys.Escape) { _learningNesKey = null; Hint("已取消学习"); }
            else BindLearn(e.KeyCode.ToString());
            e.Handled = true; e.SuppressKeyPress = true;
            return;
        }

        // 焦点在命令按钮上时，放行其原生键盘行为（如 Enter 触发按钮）
        if (ActiveControl is Button) return;

        var pk = e.KeyCode.ToString();
        if (_keyMap.TryGetValue(pk, out var nes))
        {
            if (_heldPhysical.Add(pk)) // 仅首次按下发送 down，屏蔽系统 auto-repeat
            {
                SendLine(NesKeys.Down(nes));
                SetKeyPressed(nes, true);
            }
            e.Handled = true; e.SuppressKeyPress = true;
        }
    }

    private void Form_KeyUp(object? sender, KeyEventArgs e)
    {
        var pk = e.KeyCode.ToString();
        if (_keyMap.TryGetValue(pk, out var nes) && _heldPhysical.Remove(pk))
        {
            SendLine(NesKeys.Up(nes));
            SetKeyPressed(nes, false);
            e.Handled = true; e.SuppressKeyPress = true;
        }
    }

    #endregion

    #region 按键映射学习

    private void StartLearn(string nesKey, string label)
    {
        _learningNesKey = nesKey;
        Hint($"请按下要绑定到 [{label}] 的物理键…（Esc 取消）");
    }

    private void BindLearn(string physicalKey)
    {
        if (_learningNesKey == null) return;
        if (physicalKey is "ShiftKey" or "ControlKey" or "Menu" or "None")
        {
            Hint("请按一个非修饰键");
            return;
        }
        var target = _learningNesKey;
        _learningNesKey = null;

        // 保持 1:1：移除已绑到 target 的旧物理键，以及该物理键的旧绑定
        var toRemove = new List<string>();
        foreach (var kv in _keyMap)
        {
            if (kv.Value == target || kv.Key == physicalKey) toRemove.Add(kv.Key);
        }
        foreach (var k in toRemove) _keyMap.Remove(k);
        _keyMap[physicalKey] = target;

        RefreshMappingUI();
        SaveConfig();
        Hint($"已绑定 {target} → {physicalKey}");
    }

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

    private void InitPort()
    {
        _port.RtsEnable = true;
        _port.DtrEnable = true;
        _port.ReadTimeout = 500;
        _port.WriteTimeout = 500;
    }

    private void RefreshPorts()
    {
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
        string[] ports;
        try { ports = SerialPort.GetPortNames(); }
        catch { return; }

        if (_port.IsOpen && Array.IndexOf(ports, _port.PortName) < 0)
        {
            try { _port.Close(); } catch { }
            UpdateOpenState(false);
            _lblPort.Text = $"{_port.PortName} 已断开";
            return;
        }
        string cur = _port.IsOpen ? _port.PortName : _cboPort.Text;
        _cboPort.Items.Clear();
        if (ports.Length > 0) _cboPort.Items.AddRange(ports);
        if (Array.IndexOf(ports, cur) >= 0) _cboPort.Text = cur;
        else if (ports.Length > 0 && !_port.IsOpen) _cboPort.SelectedIndex = 0;
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
                _port.PortName = _cboPort.Text;
                _port.BaudRate = int.Parse(_cboBaud.Text);
                _port.Open();
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
            try { _port.Close(); } catch { }
            UpdateOpenState(false);
        }
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
        if (!_port.IsOpen) return;
        int n = _port.BytesToRead;
        if (n <= 0) return;
        byte[] buf = new byte[n];
        int r = _port.Read(buf, 0, n);
        if (r <= 0) return;
        byte[] data = new byte[r];
        Array.Copy(buf, data, r);
        BeginInvoke(new Action<byte[]>(OnData), data);
    }

    private void OnData(byte[] data)
    {
        _recvBytes += data.Length;
        _lblRecvStat.Text = $"接收: {_recvBytes} 字节";
        char[] ch = new char[_decoder.GetCharCount(data, 0, data.Length)];
        int n = _decoder.GetChars(data, 0, data.Length, ch, 0);
        _recvText.Append(ch, 0, n);
        if (_recvText.Length > 20000) _recvText.Remove(0, _recvText.Length - 20000);
        _txtLog.Text = _recvText.ToString();
        _txtLog.SelectionStart = _txtLog.TextLength;
        _txtLog.ScrollToCaret();
    }

    private void SendLine(string line)
    {
        if (!_port.IsOpen)
        {
            Hint("! 串口未打开");
            return;
        }
        try
        {
            var bytes = _enc.GetBytes(line + "\r\n");
            _port.Write(bytes, 0, bytes.Length);
        }
        catch (Exception ex)
        {
            Hint("! 发送失败: " + ex.Message);
        }
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
        if (cfg.KeyMap != null && cfg.KeyMap.Count > 0) _keyMap = cfg.KeyMap;
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
            KeyMap = _keyMap
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
