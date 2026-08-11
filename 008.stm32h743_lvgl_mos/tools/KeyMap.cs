using System.Collections.Generic;

namespace NesPadTool;

/// <summary>
/// 物理键盘按键 → NES 虚拟键 的映射。
/// 键名用 Keys 枚举的 ToString()（如 "W"、"LShiftKey"、"Return"、"Up"）。
/// </summary>
public static class KeyMap
{
    /// <summary>默认映射：WASD=方向，JK=A/B，左Shift=Select，Enter=Start。</summary>
    public static Dictionary<string, string> Default()
    {
        return new Dictionary<string, string>
        {
            ["W"] = "up",
            ["S"] = "down",
            ["A"] = "left",
            ["D"] = "right",
            ["J"] = "a",
            ["K"] = "b",
            ["LShiftKey"] = "select",
            ["Return"] = "start",
        };
    }
}
