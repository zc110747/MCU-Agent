namespace NesPadTool;

/// <summary>
/// NES 虚拟键定义与串口命令构造。键名与固件 app_cmd.c 完全一致（小写）。
/// </summary>
public static class NesKeys
{
    /// <summary>虚拟键（固件名, 显示标签）。顺序即手柄面板布局顺序。</summary>
    public static readonly (string Key, string Label)[] Buttons =
    {
        ("up",      "↑"),
        ("down",    "↓"),
        ("left",    "←"),
        ("right",   "→"),
        ("a",       "A"),
        ("b",       "B"),
        ("select",  "SELECT"),
        ("start",   "START"),
    };

    /// <summary>按下并保持：down &lt;name&gt;</summary>
    public static string Down(string name) => "down " + name;

    /// <summary>抬起：up &lt;name&gt;</summary>
    public static string Up(string name) => "up " + name;

    /// <summary>点按一下（自动释放）：key &lt;name&gt;</summary>
    public static string Tap(string name) => "key " + name;

    /// <summary>释放全部按键：release</summary>
    public static string Release() => "release";

    /// <summary>其他菜单/ROM 快捷命令（复用固件协议）。</summary>
    public static class Cmd
    {
        public static string OpenNes => "open nes";
        public static string RomList => "rom list";
        public static string RomLoad(int i) => "rom load " + i;
        public static string RomStop => "rom stop";
        public static string RomInfo => "rom info";
        public static string Status => "status";
        public static string Menu => "menu";

        /// <summary>截取当前 OLED 页面为 JPEG 存到 SD 卡 1:/catch/（固件 screen_cap.c）。</summary>
        public static string Capture => "cap";
    }
}
