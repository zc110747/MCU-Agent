using System.Windows.Forms;

namespace NesPadTool;

internal static class Program
{
    [STAThread]
    private static void Main()
    {
        // 捕获 UI 线程未处理异常，避免直接弹出 JIT 调试对话框（参考记账工具）
        Application.SetUnhandledExceptionMode(UnhandledExceptionMode.CatchException);
        Application.ThreadException += (_, ex) =>
        {
            var e = ex.Exception;
            MessageBox.Show(
                "程序遇到一个意外错误，已阻止崩溃：\n" + (e?.GetType().Name + "：" + e?.Message ?? "未知错误"),
                "出错了", MessageBoxButtons.OK, MessageBoxIcon.Error);
        };

        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        Application.Run(new MainForm());
    }
}
