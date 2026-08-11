using System.Collections.Generic;
using System.Drawing;

namespace NesPadTool;

/// <summary>
/// 一套 UI 皮肤（配色主题）。所有颜色集中在此，便于统一切换。
/// 复用记账工具 Skin 设计：record + 预设列表，所有控件颜色从 Skin 取，保证图标/按钮显示一致。
/// </summary>
public record Skin(
    string Name,
    Color Primary,    // 主强调色（顶栏 / 主操作按钮）
    Color Secondary,  // 次要操作 / 手柄按键高亮
    Color Success,    // 成功 / 导出
    Color Neutral,    // 中性操作（释放 / 清除）
    Color Bg,         // 窗体背景
    Color Panel,      // 卡片 / 面板 / 手柄按键面
    Color InputBg,    // 输入框 / 下拉框背景
    Color HeaderBg,   // 组标题背景
    Color HeaderFore, // 组标题文字
    Color Text,       // 默认文字
    Color SubText,    // 次要文字
    Color GridAlt,    // 备用底色
    Color SelBg       // 选中底色
);

public static class SkinPresets
{
    public static readonly List<Skin> Skins = new()
    {
        // 默认·蓝
        new Skin("默认·蓝",
            Primary:    Color.FromArgb(0x1E, 0x88, 0xE5),
            Secondary:  Color.FromArgb(0x5C, 0x6B, 0xC0),
            Success:    Color.FromArgb(0x43, 0xA0, 0x47),
            Neutral:    Color.FromArgb(0x9E, 0x9E, 0x9E),
            Bg:         Color.FromArgb(0xF5, 0xF6, 0xF8),
            Panel:      Color.White,
            InputBg:    Color.White,
            HeaderBg:   Color.FromArgb(0xFA, 0xFA, 0xFA),
            HeaderFore: Color.FromArgb(0x37, 0x47, 0x4F),
            Text:       Color.FromArgb(0x26, 0x32, 0x38),
            SubText:    Color.FromArgb(0x60, 0x7D, 0x8B),
            GridAlt:    Color.FromArgb(0xF7, 0xF9, 0xFB),
            SelBg:      Color.FromArgb(0xBB, 0xDE, 0xFB)),

        // 清新·绿
        new Skin("清新·绿",
            Primary:    Color.FromArgb(0x2E, 0x9E, 0x5B),
            Secondary:  Color.FromArgb(0x00, 0x89, 0x7B),
            Success:    Color.FromArgb(0x43, 0xA0, 0x47),
            Neutral:    Color.FromArgb(0x9E, 0x9E, 0x9E),
            Bg:         Color.FromArgb(0xF1, 0xF8, 0xF4),
            Panel:      Color.White,
            InputBg:    Color.White,
            HeaderBg:   Color.FromArgb(0xED, 0xF7, 0xF1),
            HeaderFore: Color.FromArgb(0x2E, 0x5D, 0x43),
            Text:       Color.FromArgb(0x24, 0x3B, 0x30),
            SubText:    Color.FromArgb(0x5A, 0x7A, 0x6A),
            GridAlt:    Color.FromArgb(0xF2, 0xF9, 0xF5),
            SelBg:      Color.FromArgb(0xC8, 0xE6, 0xC9)),

        // 暖橙·橙
        new Skin("暖橙·橙",
            Primary:    Color.FromArgb(0xFB, 0x8C, 0x00),
            Secondary:  Color.FromArgb(0xF4, 0x51, 0x1E),
            Success:    Color.FromArgb(0x8B, 0xC3, 0x4A),
            Neutral:    Color.FromArgb(0xBD, 0xBD, 0xBD),
            Bg:         Color.FromArgb(0xFF, 0xF6, 0xED),
            Panel:      Color.White,
            InputBg:    Color.White,
            HeaderBg:   Color.FromArgb(0xFF, 0xF0, 0xE0),
            HeaderFore: Color.FromArgb(0x7A, 0x4B, 0x12),
            Text:       Color.FromArgb(0x3E, 0x2C, 0x12),
            SubText:    Color.FromArgb(0x8D, 0x6E, 0x4B),
            GridAlt:    Color.FromArgb(0xFF, 0xF7, 0xEE),
            SelBg:      Color.FromArgb(0xFF, 0xE0, 0xB2)),

        // 暗夜·深
        new Skin("暗夜·深",
            Primary:    Color.FromArgb(0x7C, 0x4D, 0xFF),
            Secondary:  Color.FromArgb(0x26, 0xC6, 0xDA),
            Success:    Color.FromArgb(0x66, 0xBB, 0x6A),
            Neutral:    Color.FromArgb(0x54, 0x6E, 0x7A),
            Bg:         Color.FromArgb(0x23, 0x25, 0x2B),
            Panel:      Color.FromArgb(0x2D, 0x2F, 0x36),
            InputBg:    Color.FromArgb(0x38, 0x3B, 0x43),
            HeaderBg:   Color.FromArgb(0x35, 0x38, 0x42),
            HeaderFore: Color.FromArgb(0xEC, 0xEF, 0xF1),
            Text:       Color.FromArgb(0xE6, 0xE6, 0xE6),
            SubText:    Color.FromArgb(0xB0, 0xBE, 0xC5),
            GridAlt:    Color.FromArgb(0x32, 0x35, 0x3D),
            SelBg:      Color.FromArgb(0x53, 0x6D, 0xFE)),
    };
}
