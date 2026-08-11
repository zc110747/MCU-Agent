using System.Collections.Generic;
using System.IO;
using System.Text.Json;

namespace NesPadTool;

/// <summary>
/// 本地配置：串口信息(port/baud) + 皮肤名 + 物理键→NES虚拟键映射。
/// 存为程序运行目录下的 nespad.config.json（参考记账工具 AppConfig 的 JSON 持久化方式）。
/// </summary>
public class AppConfigData
{
    public string? Port { get; set; }
    public int Baud { get; set; } = 115200;
    public string? SkinName { get; set; }
    public Dictionary<string, string>? KeyMap { get; set; }
}

public static class AppConfig
{
    private static string FilePath =>
        Path.Combine(Directory.GetCurrentDirectory(), "nespad.config.json");

    public static AppConfigData Load()
    {
        try
        {
            if (File.Exists(FilePath))
            {
                var d = JsonSerializer.Deserialize<AppConfigData>(File.ReadAllText(FilePath));
                if (d != null) return d;
            }
        }
        catch { /* 读取失败则用默认 */ }
        return new AppConfigData();
    }

    public static void Save(AppConfigData d)
    {
        try
        {
            File.WriteAllText(FilePath, JsonSerializer.Serialize(d,
                new JsonSerializerOptions { WriteIndented = true }));
        }
        catch { /* 写入失败不影响主流程 */ }
    }
}
