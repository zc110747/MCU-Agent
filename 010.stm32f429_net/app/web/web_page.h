#ifndef __WEB_PAGE_H
#define __WEB_PAGE_H

/* Simple status page stored in flash (.rodata). Served by the HTTP server. */
static const char HTTP_PAGE[] =
"<!DOCTYPE html>\r\n"
"<html lang=\"zh\">\r\n"
"<head>\r\n"
"  <meta charset=\"utf-8\">\r\n"
"  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\r\n"
"  <title>STM32F429 Web Server</title>\r\n"
"  <style>\r\n"
"    body{font-family:Segoe UI,Arial,sans-serif;margin:0;background:#0f172a;color:#e2e8f0}\r\n"
"    .card{max-width:640px;margin:48px auto;background:#1e293b;border-radius:12px;padding:28px 32px;box-shadow:0 8px 24px rgba(0,0,0,.35)}\r\n"
"    h1{color:#38bdf8;margin-top:0}\r\n"
"    .row{display:flex;justify-content:space-between;border-bottom:1px solid #334155;padding:10px 0}\r\n"
"    .k{color:#94a3b8}.v{font-weight:600;color:#f8fafc}\r\n"
"    .ok{color:#4ade80}.tag{display:inline-block;background:#38bdf8;color:#0f172a;border-radius:6px;padding:2px 10px;font-size:13px}\r\n"
"  </style>\r\n"
"</head>\r\n"
"<body>\r\n"
"  <div class=\"card\">\r\n"
"    <h1>STM32F429IGT6 Web Server</h1>\r\n"
"    <p><span class=\"tag\">LwIP / raw API</span> &nbsp; <span class=\"ok\">RUNNING</span></p>\r\n"
"    <div class=\"row\"><span class=\"k\">MCU</span><span class=\"v\">STM32F429IGT6 (Cortex-M4 @ 180MHz)</span></div>\r\n"
"    <div class=\"row\"><span class=\"k\">IP Address</span><span class=\"v\">192.168.10.99</span></div>\r\n"
"    <div class=\"row\"><span class=\"k\">Gateway</span><span class=\"v\">192.168.10.1</span></div>\r\n"
"    <div class=\"row\"><span class=\"k\">Subnet Mask</span><span class=\"v\">255.255.255.0</span></div>\r\n"
"    <div class=\"row\"><span class=\"k\">PHY</span><span class=\"v\">LAN8720A (RMII)</span></div>\r\n"
"    <div class=\"row\"><span class=\"k\">Protocol</span><span class=\"v\">HTTP/1.1</span></div>\r\n"
"    <p style=\"color:#94a3b8;font-size:13px\">Page is embedded in internal Flash. Ping 192.168.10.99 to verify the network link.</p>\r\n"
"  </div>\r\n"
"</body>\r\n"
"</html>\r\n";

#endif /* __WEB_PAGE_H */
