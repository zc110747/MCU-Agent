// ToggleSwitch.cs — 暗色 ON/OFF 开关控件（带文字标签）
using System;
using System.ComponentModel;
using System.Drawing;
using System.Windows.Forms;

namespace SnmpDesktop
{
    public class ToggleSwitch : Control
    {
        private bool state;
        private string caption = "";
        private const int TrackW = 52, TrackH = 26, Thumb = 20, Pad = 3;

        public ToggleSwitch()
        {
            Width = 160; Height = 26;
            DoubleBuffered = true;
            Cursor = Cursors.Hand;
        }

        [DesignerSerializationVisibility(DesignerSerializationVisibility.Visible)]
        public bool State
        {
            get => state;
            set { if (state == value) return; state = value; Invalidate(); }
        }

        [DesignerSerializationVisibility(DesignerSerializationVisibility.Visible)]
        public string Caption
        {
            get => caption;
            set { caption = value; Invalidate(); }
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            // 开关轨道
            int tx = 0, ty = (Height - TrackH) / 2;
            using var track = new SolidBrush(state ? Color.FromArgb(70, 201, 139) : Color.FromArgb(60, 64, 72));
            g.FillRoundedRectangle(track, new Rectangle(tx, ty, TrackW, TrackH), 13);
            // 滑块
            int thumbX = state ? tx + TrackW - Thumb - Pad : tx + Pad;
            int thumbY = ty + (TrackH - Thumb) / 2;
            using var thumb = new SolidBrush(Color.White);
            g.FillRoundedRectangle(thumb, new Rectangle(thumbX, thumbY, Thumb, Thumb), 10);
            // 文字标签
            if (!string.IsNullOrEmpty(caption))
            {
                using var br = new SolidBrush(Color.LightGray);
                using var f = new Font("Microsoft YaHei", 11);
                g.DrawString(caption, f, br, tx + TrackW + 8, ty + 3);
            }
        }

        protected override void OnClick(EventArgs e)
        {
            base.OnClick(e);
            // 不在此自动翻转 State：由订阅 Click 的处理器（MainForm.OnToggle）统一决定，
            // 否则会与处理器里的 tg.State = next 形成双重翻转。
        }
    }

    // 圆角矩形扩展
    internal static class GraphicsRoundExt
    {
        public static void FillRoundedRectangle(this Graphics g, Brush brush, Rectangle rect, int radius)
        {
            using var path = new System.Drawing.Drawing2D.GraphicsPath();
            int r = Math.Min(radius, rect.Height / 2);
            path.AddArc(rect.X, rect.Y, r * 2, r * 2, 180, 90);
            path.AddArc(rect.X + rect.Width - r * 2, rect.Y, r * 2, r * 2, 270, 90);
            path.AddArc(rect.X + rect.Width - r * 2, rect.Y + rect.Height - r * 2, r * 2, r * 2, 0, 90);
            path.AddArc(rect.X, rect.Y + rect.Height - r * 2, r * 2, r * 2, 90, 90);
            path.CloseFigure();
            g.FillPath(brush, path);
        }
    }
}
