// VisualTest.cs — 直接打开真实 snmp_desktop 主窗体，逐页渲染并截图到本地 PNG，
// 用于在无人工介入时验证每个页面都能完整显示、不被裁切。
//
// 用法：snmp_desktop_visualtest.exe <host> [port] [outdir]
//   - 启动后自动"启动轮询"拉一次数据，再对 6 个菜单页逐一截图。
//   - 每张图文件名 page_xx.png，旁边生成 page_xx.txt 记录该页可见控件数量与是否出现滚动条。

using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Windows.Forms;
using SnmpDesktop;

static class Program
{
    [STAThread]
    static void Main(string[] args)
    {
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);

        string host = args.Length > 0 ? args[0] : "192.168.10.99";
        int port = args.Length > 1 ? int.Parse(args[1]) : 161;
        string outDir = args.Length > 2 ? args[2] : Path.Combine(Directory.GetCurrentDirectory(), "shots");
        Directory.CreateDirectory(outDir);

        // 控制布局的最小尺寸，保证窗口足够大以暴露潜在裁切
        var form = new MainForm
        {
            ClientSize = new Size(980, 640),
            StartPosition = FormStartPosition.Manual,
            Location = new Point(0, 0),
        };
        form.Show();
        Application.DoEvents();

        // 填连接参数 + 启动轮询
        SetText(form, "txtHost", host);
        SetText(form, "txtPort", port.ToString());
        ClickButton(form, "btnStart");
        System.Threading.Thread.Sleep(1500); // 等首轮刷新完成
        Application.DoEvents();

        // 6 个菜单页依次截图
        string[] pages = { "系统概念", "设备信息", "硬件监控", "传感器监控", "网络状态", "参数设置" };
        int pass = 0, fail = 0;
        for (int i = 0; i < pages.Length; i++)
        {
            ClickMenu(form, pages[i]);
            Application.DoEvents();
            System.Threading.Thread.Sleep(400); // 等刷新+布局稳定
            Application.DoEvents();
            if (args.Length > 3 && args[3] == "dump") DumpSensors(form);

            string png = Path.Combine(outDir, $"page_{i + 1:D2}_{pages[i]}.png");
            form.Refresh();
            Application.DoEvents();
            System.Threading.Thread.Sleep(50);
            Application.DoEvents();
            // 用 PrintWindow(PW_RENDERFULLCONTENT=2) 强制重绘全部控件，对抗锯齿/OwnerDraw 完整支持
            using (var bmp = new Bitmap(form.Width, form.Height))
            {
                using (var g = Graphics.FromImage(bmp))
                {
                    IntPtr hdc = g.GetHdc();
                    try { PrintWindow(form.Handle, hdc, PW_RENDERFULLCONTENT); }
                    finally { g.ReleaseHdc(hdc); }
                }
                bmp.Save(png, ImageFormat.Png);
            }

            // 统计该页可见控件 + 是否存在滚动条
            int visible = CountVisible(form);
            bool scrolled = HasScrollbar(form);
            string note = $"page={pages[i]} visibleControls={visible} hasScrollbar={scrolled}";
            File.WriteAllText(Path.Combine(outDir, $"page_{i + 1:D2}_{pages[i]}.txt"), note);
            Console.WriteLine($"[{(scrolled ? "OK" : "WARN")}] {note} -> {png}");
            if (visible > 0) pass++; else fail++;
        }

        Console.WriteLine($"\n=== VISUAL TEST: pages={pages.Length} visible>0={pass} empty={fail} ===");
        Console.WriteLine($"截图目录: {outDir}");
        form.Close();
        Application.Exit();
    }

    // ---- 反射助手：通过字段名访问 MainForm 私有控件 ----
    static System.Reflection.FieldInfo F(object o, string name) =>
        o.GetType().GetField(name, System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance)!;

    static void SetText(Form form, string field, string v)
    {
        var tb = (TextBox)F(form, field).GetValue(form)!;
        tb.Text = v;
    }

    static void ClickButton(Form form, string field)
    {
        var btn = (Button)F(form, field).GetValue(form)!;
        btn.PerformClick();
    }

    // 点击左侧菜单：直接调用 MainForm 私有 SelectMenu(index)
    static void ClickMenu(Form form, string text)
    {
        var labels = (System.Collections.Generic.List<Label>)F(form, "_menuLabels").GetValue(form)!;
        int idx = labels.FindIndex(l => l.Text == text);
        if (idx < 0) throw new Exception("菜单未找到: " + text);
        var mi = form.GetType().GetMethod("SelectMenu", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance)!;
        mi.Invoke(form, new object[] { idx });
    }

    static int CountVisible(Control root)
    {
        int n = 0;
        foreach (Control c in root.Controls)
        {
            if (c.Visible && c.Width > 0 && c.Height > 0) n++;
            n += CountVisible(c);
        }
        return n;
    }

    // 调试：打印 sensor 卡片数量与位置
    static void DumpSensors(Form form)
    {
        var flp = (Panel)F(form, "pnlSensors").GetValue(form)!;
        int cards = 0;
        var positions = new List<string>();
        foreach (Control c in flp.Controls)
        {
            if (c is Panel sc && sc.AutoScroll)
            {
                foreach (Control cc in sc.Controls)
                {
                    if (cc is Panel card)
                    {
                        cards++;
                        positions.Add($"({card.Location.X},{card.Location.Y})");
                    }
                }
            }
        }
        Console.WriteLine($"[DEBUG sensors] cardCount={cards} positions={string.Join(" ", positions)}");
    }

    // 检测右侧内容区是否出现垂直滚动条（AutoScroll 容器有滚动条意味着内容超高，但应能滚动查看全部）
    static bool HasScrollbar(Form form)
    {
        var pnl = (Panel)F(form, "pnlContent").GetValue(form)!;
        foreach (Control c in pnl.Controls)
        {
            if (c is Panel p && p.AutoScroll && p.VerticalScroll != null && p.VerticalScroll.Visible)
                return true;
        }
        return false;
    }

    // Win32 PrintWindow：用目标窗口自身绘制全部内容到我们提供的 DC
    const uint PW_RENDERFULLCONTENT = 0x00000002;
    [System.Runtime.InteropServices.DllImport("user32.dll")]
    static extern bool PrintWindow(IntPtr hwnd, IntPtr hdcBlt, uint nFlags);
}
