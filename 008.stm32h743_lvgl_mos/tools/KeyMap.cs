using System.Collections.Generic;

namespace NesPadTool;

/// <summary>
/// 物理键盘按键 → NES 虚拟键 的固定映射。
/// 键名用 Keys 枚举的 ToString()（如 "W"、"Return"、"Up"）。
/// </summary>
public static class KeyMap
{
    /// <summary>固定映射：上-w / 下-s / 左-a / 右-d / SELECT-q / START-e / A-j / B-k。</summary>
    public static Dictionary<string, string> Default()
    {
        return new Dictionary<string, string>
        {
            ["W"] = "up",
            ["S"] = "down",
            ["A"] = "left",
            ["D"] = "right",
            ["Q"] = "select",
            ["E"] = "start",
            ["J"] = "a",
            ["K"] = "b",
        };
    }
}
