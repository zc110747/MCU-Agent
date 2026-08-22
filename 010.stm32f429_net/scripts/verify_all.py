#!/usr/bin/env python3
# verify_all.py - STM32F429_NET 功能验证（HTTP/HTTPS/REST API）
#
# 用法:
#   python scripts/verify_all.py                 # 仅网络端点验证
#   python scripts/verify_all.py --serial COM3   # + 启动日志抓取(默认8s)
#   python scripts/verify_all.py --host 192.168.10.99 --serial COM19 --secs 10
#
# 退出码: 0=全部通过, 1=有失败项
import socket
import ssl
import sys
import json
import time
import argparse

try:
    import urllib.request as ureq
except Exception:
    ureq = None

HOST = "192.168.10.99"
PORT_HTTP = 80
PORT_HTTPS = 443

results = []  # (name, ok, detail)


def record(name, ok, detail=""):
    results.append((name, ok, detail))
    tag = "PASS" if ok else "FAIL"
    print(f"[{tag}] {name}" + (f"  -- {detail}" if detail else ""))


# ------------------------------------------------------------------
# 1) HTTP 首页（应来自 SD 卡 web/index.html，非 flash fallback）
# ------------------------------------------------------------------
def verify_http_index(host):
    try:
        with socket.create_connection((host, PORT_HTTP), timeout=5) as s:
            s.sendall(b"GET / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
            data = b""
            while True:
                try:
                    chunk = s.recv(4096)
                except Exception:
                    break
                if not chunk:
                    break
                data += chunk
                if len(data) > 65536:
                    break
        if not data:
            record("HTTP /", False, "no response")
            return
        body = data.split(b"\r\n\r\n", 1)[-1]
        text = body.decode("utf-8", "replace")
        # SD Vue 页特征: <div id="app"> 或 ./assets/ 引用
        is_sd_page = ('<div id="app"' in text) or ('id="app"' in text) or ("./assets/" in text)
        # flash fallback 特征: 标题 "STM32F429IGT6 Web Server"
        is_flash_fallback = "STM32F429IGT6 Web Server" in text
        if is_sd_page:
            record("HTTP / (SD Vue page)", True, f"{len(body)} bytes, has #app/assets")
        elif is_flash_fallback:
            record("HTTP / (SD Vue page)", False, "served flash fallback -> SD web/index.html 缺失")
        else:
            record("HTTP / (SD Vue page)", False, f"unexpected body head: {text[:60]!r}")
    except Exception as e:
        record("HTTP / (SD Vue page)", False, repr(e))


# ------------------------------------------------------------------
# 2) HTTPS 握手 + 首页
# ------------------------------------------------------------------
def verify_https_index(host):
    try:
        s = socket.create_connection((host, PORT_HTTPS), timeout=5)
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        ss = ctx.wrap_socket(s, server_hostname="stm32f429.local")
        ver = ss.version()
        ss.sendall(b"GET / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
        data = b""
        while True:
            try:
                c = ss.recv(4096)
            except Exception:
                break
            if not c:
                break
            data += c
            if len(data) > 65536:
                break
        ss.close()
        body = data.split(b"\r\n\r\n", 1)[-1]
        text = body.decode("utf-8", "replace")
        is_sd_page = ('<div id="app"' in text) or ('id="app"' in text) or ("./assets/" in text)
        is_flash_fallback = "STM32F429IGT6 Web Server" in text
        if is_sd_page:
            record("HTTPS / (TLS1.2 + SD page)", True, f"{ver}, {len(body)} bytes")
        elif is_flash_fallback:
            record("HTTPS / (TLS1.2 + SD page)", False, f"{ver}, served flash fallback -> SD web/index.html 缺失")
        else:
            record("HTTPS / (TLS1.2 + SD page)", False, f"{ver}, body head: {text[:60]!r}")
    except Exception as e:
        record("HTTPS / (TLS1.2 + SD page)", False, repr(e))


# ------------------------------------------------------------------
# 3) /api/hardware JSON
# ------------------------------------------------------------------
def verify_api_hardware(host):
    try:
        with socket.create_connection((host, PORT_HTTP), timeout=5) as s:
            s.sendall(b"GET /api/hardware HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
            data = b""
            while True:
                try:
                    c = s.recv(4096)
                except Exception:
                    break
                if not c:
                    break
                data += c
            body = data.split(b"\r\n\r\n", 1)[-1]
        try:
            j = json.loads(body)
        except Exception:
            record("/api/hardware", False, f"not JSON: {body[:80]!r}")
            return
        need = ["mcu", "ap3216c", "mpu9250", "led", "beep"]
        missing = [k for k in need if k not in j]
        if missing:
            record("/api/hardware", False, f"missing keys: {missing}")
            return
        # ap3216c / mpu9250 子字段存在性
        ap = j.get("ap3216c", {})
        imu = j.get("mpu9250", {})
        if "lux" not in ap or "ax" not in imu:
            record("/api/hardware", False, f"incomplete sensor fields: ap={ap} imu={imu}")
            return
        record("/api/hardware", True,
               f"lux={ap.get('lux')} ax={imu.get('ax')} ay={imu.get('ay')} az={imu.get('az')}")
    except Exception as e:
        record("/api/hardware", False, repr(e))


# ------------------------------------------------------------------
# 4) /api/network GET + POST /api/control (LED/BEEP)
# ------------------------------------------------------------------
def _http_post(host, path, payload):
    req = ("POST %s HTTP/1.1\r\nHost: x\r\nContent-Type: application/json\r\n"
           "Content-Length: %d\r\nConnection: close\r\n\r\n%s" %
           (path, len(payload), payload))
    with socket.create_connection((host, PORT_HTTP), timeout=5) as s:
        s.sendall(req.encode())
        data = b""
        while True:
            try:
                c = s.recv(4096)
            except Exception:
                break
            if not c:
                break
            data += c
        return data.split(b"\r\n\r\n", 1)[-1]


def verify_api_network_and_control(host):
    # GET network
    try:
        with socket.create_connection((host, PORT_HTTP), timeout=5) as s:
            s.sendall(b"GET /api/network HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
            data = b""
            while True:
                try:
                    c = s.recv(4096)
                except Exception:
                    break
                if not c:
                    break
                data += c
            body = data.split(b"\r\n\r\n", 1)[-1]
        j = json.loads(body)
        ok = all(k in j for k in ("ip", "mask", "gw", "mac"))
        record("/api/network GET", ok, body.decode("utf-8", "replace") if ok else f"bad: {body[:60]!r}")
    except Exception as e:
        record("/api/network GET", False, repr(e))

    # POST control led=1
    try:
        r1 = _http_post(host, "/api/control", '{"led":1,"beep":0}')
        ok1 = b'"ok":true' in r1 or b'"ok": true' in r1
        # POST control beep=1
        r2 = _http_post(host, "/api/control", '{"led":1,"beep":1}')
        ok2 = b'"ok":true' in r2 or b'"ok": true' in r2
        # 关掉，避免一直响
        _http_post(host, "/api/control", '{"led":0,"beep":0}')
        record("POST /api/control (LED/BEEP on->off)", ok1 and ok2,
               f"led_on={ok1} beep_on={ok2} (已复位关闭)")
    except Exception as e:
        record("POST /api/control (LED/BEEP on->off)", False, repr(e))


# ------------------------------------------------------------------
# 5) POST /api/network 持久化（改 IP -> 提示需断电重启验证，脚本只验证返回 ok）
# ------------------------------------------------------------------
def verify_api_network_set(host):
    try:
        r = _http_post(host, "/api/network",
                       '{"ip":"192.168.10.99","mask":"255.255.255.0","gw":"192.168.10.1","mac":"00:80:E1:42:10:99"}')
        ok = b'"ok":true' in r or b'"ok": true' in r
        record("POST /api/network (persist)", ok,
               "返回 ok 即写入 SD netcfg.ini (需断电重启确认生效)")
    except Exception as e:
        record("POST /api/network (persist)", False, repr(e))


# ------------------------------------------------------------------
# 可选: 串口启动日志抓取
# ------------------------------------------------------------------
def capture_serial(port, secs):
    try:
        import serial
    except ImportError:
        print("[WARN] pyserial 未安装，跳过串口日志 (pip install pyserial)")
        return
    try:
        s = serial.Serial(port, 115200, timeout=0.2)
    except Exception as e:
        print(f"[WARN] 无法打开串口 {port}: {e}")
        return
    buf = bytearray()
    stop = time.time() + secs

    def reader():
        while time.time() < stop:
            try:
                n = s.in_waiting
            except Exception:
                break
            if n:
                buf.extend(s.read(n))
            else:
                time.sleep(0.02)

    import threading
    t = threading.Thread(target=reader, daemon=True)
    t.start()
    t.join()
    text = bytes(buf).decode("utf-8", "replace")
    print("\n===== 串口启动日志 ({}s) =====".format(secs))
    print(text, end="")
    print("\n===== 日志结束 ({} bytes) =====".format(len(buf)))
    # 关键标记检查
    marks = {
        "SDIO init OK": "SDIO: init OK" in text,
        "FatFs mount OK": "FatFs: mount OK" in text,
        "HTTP listening": "HTTP server" in text or "listening" in text,
        "HTTPS listening": "HTTPS server: listening" in text,
        "scheduler start": "scheduler starting" in text,
    }
    print("日志关键标记:")
    for k, v in marks.items():
        print(f"  [{'Y' if v else 'N'}] {k}")
    s.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default=HOST)
    ap.add_argument("--serial", default=None, help="串口如 COM3 / COM19")
    ap.add_argument("--secs", type=float, default=8.0, help="串口抓取秒数")
    args = ap.parse_args()

    print(f"=== STM32F429_NET 功能验证 @ {args.host} ===")
    print("目标: SDIO正常基础上验证网络/网页/API 功能\n")

    verify_http_index(args.host)
    verify_https_index(args.host)
    verify_api_hardware(args.host)
    verify_api_network_and_control(args.host)
    verify_api_network_set(args.host)

    if args.serial:
        capture_serial(args.serial, args.secs)

    # 汇总
    npass = sum(1 for _, ok, _ in results if ok)
    nfail = len(results) - npass
    print(f"\n=== 汇总: {npass} PASS / {nfail} FAIL (共 {len(results)} 项) ===")
    if nfail:
        print("失败项:")
        for name, ok, detail in results:
            if not ok:
                print(f"  - {name}: {detail}")
        sys.exit(1)
    sys.exit(0)


if __name__ == "__main__":
    main()
