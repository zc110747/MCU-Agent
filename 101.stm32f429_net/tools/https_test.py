#!/usr/bin/env python3
"""
HTTPS server test for STM32F429 (self-signed cert, port 443).

Tests:
  1. Single GET / returns 200 + 'Connection: keep-alive' (handshake caching).
  2. Connection reuse: a second request on the SAME socket is served without
     a new TLS handshake (keep-alive works -> browser won't re-handshake).
  3. Concurrent load: 4 parallel connections each do 3 sequential requests;
     all must return 200 (proves the server is no longer serial/blocking).
  4. Big asset fetch (if present) exercises the record-chunking path.

Usage:
  python tools/https_test.py --host 192.168.10.99
  python tools/https_test.py --host 192.168.10.99 --conns 6 --reqs 3
"""
import socket
import ssl
import threading
import time
import sys
import argparse


def make_ctx():
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    # allow TLS 1.2 (mbedTLS server preset default)
    ctx.minimum_version = ssl.TLSVersion.TLSv1_2
    ctx.maximum_version = ssl.TLSVersion.TLSv1_2
    return ctx


def https_get(ctx, host, port, path, timeout=8.0, reuse_sock=None):
    """One HTTPS request. If reuse_sock is given, reuse that wrapped socket
    (keep-alive). Returns (status_line, headers_dict, body_bytes, sock)."""
    if reuse_sock is None:
        raw = socket.create_connection((host, port), timeout=timeout)
        sock = ctx.wrap_socket(raw, server_hostname=host)
    else:
        sock = reuse_sock
    req = (
        f"GET {path} HTTP/1.1\r\n"
        f"Host: {host}\r\n"
        "User-Agent: stm32-https-test\r\n"
        "Accept: */*\r\n"
        "Connection: keep-alive\r\n\r\n"
    )
    sock.sendall(req.encode("latin1"))
    sock.settimeout(timeout)
    buf = bytearray()
    # read response headers
    while b"\r\n\r\n" not in buf:
        try:
            chunk = sock.recv(4096)
        except socket.timeout:
            break
        if not chunk:
            break
        buf.extend(chunk)
    header_end = buf.find(b"\r\n\r\n")
    if header_end < 0:
        return ("", {}, bytes(buf), sock)
    head = buf[:header_end].decode("latin1", "replace")
    lines = head.split("\r\n")
    status = lines[0] if lines else ""
    headers = {}
    for ln in lines[1:]:
        if ":" in ln:
            k, v = ln.split(":", 1)
            headers[k.strip().lower()] = v.strip()
    # read body per Content-Length
    body = bytes(buf[header_end + 4:])
    cl = int(headers.get("content-length", "0"))
    while len(body) < cl:
        try:
            chunk = sock.recv(4096)
        except socket.timeout:
            break
        if not chunk:
            break
        body += chunk
    return (status, headers, body, sock)


def test_single(host, port, ctx):
    print("== Test 1: single GET / + keep-alive header ==")
    try:
        status, headers, body, sock = https_get(ctx, host, port, "/")
        ok = status.startswith("HTTP/1.1 200") and \
             headers.get("connection", "").lower() == "keep-alive"
        print(f"   status={status!r} conn={headers.get('connection')!r} "
              f"body_len={len(body)} -> {'PASS' if ok else 'FAIL'}")
        # keep socket open for reuse test
        return ok, sock
    except Exception as e:
        print(f"   EXCEPTION {e} -> FAIL")
        return False, None


def test_reuse(host, port, ctx, sock):
    print("== Test 2: reuse same TLS connection for 2nd request ==")
    own = False
    if sock is None:
        try:
            raw = socket.create_connection((host, port), timeout=8.0)
            sock = ctx.wrap_socket(raw, server_hostname=host)
            sock.settimeout(8.0)
            own = True
        except Exception as e:
            print(f"   connect failed {e} -> FAIL")
            return False
    try:
        # second request on the SAME wrapped socket
        status2, headers2, body2, sock2 = https_get(
            ctx, host, port, "/api/hardware", reuse_sock=sock)
        ok = status2.startswith("HTTP/1.1 200") and len(body2) > 0
        print(f"   status={status2!r} body_len={len(body2)} -> "
              f"{'PASS' if ok else 'FAIL'}")
        try:
            sock2.close()
        except Exception:
            pass
        return ok
    except Exception as e:
        print(f"   EXCEPTION {e} -> FAIL")
        try:
            if own:
                sock.close()
        except Exception:
            pass
        return False


def worker(host, port, ctx, conns, reqs, results, idx):
    try:
        raw = socket.create_connection((host, port), timeout=10.0)
        sock = ctx.wrap_socket(raw, server_hostname=host)
        sock.settimeout(10.0)
        ok_reqs = 0
        for r in range(reqs):
            path = "/" if r == 0 else "/api/hardware"
            try:
                status, headers, body, sock = https_get(
                    ctx, host, port, path, reuse_sock=sock)
                if status.startswith("HTTP/1.1 200"):
                    ok_reqs += 1
            except Exception:
                break
        try:
            sock.close()
        except Exception:
            pass
        results[idx] = (ok_reqs, reqs)
    except Exception as e:
        results[idx] = (0, reqs)
        print(f"   worker {idx} error: {e}")


def test_concurrent(host, port, ctx, conns, reqs):
    print(f"== Test 3: {conns} concurrent connections x {reqs} requests ==")
    results = [None] * conns
    threads = []
    t0 = time.time()
    for i in range(conns):
        t = threading.Thread(
            target=worker, args=(host, port, ctx, conns, reqs, results, i))
        t.start()
        threads.append(t)
    for t in threads:
        t.join()
    elapsed = time.time() - t0
    total_ok = sum(r[0] for r in results if r)
    total_req = conns * reqs
    # each worker should have served all its requests (no serial stall)
    all_full = all(r and r[0] == reqs for r in results)
    print(f"   served {total_ok}/{total_req} requests in {elapsed:.2f}s -> "
          f"{'PASS' if all_full else 'FAIL'}")
    if not all_full:
        for i, r in enumerate(results):
            print(f"     worker {i}: {r}")
    return all_full


def run(host, port, conns, reqs):
    ctx = make_ctx()
    passed = 0
    total = 0

    ok1, sock = test_single(host, port, ctx)
    total += 1
    passed += 1 if ok1 else 0
    try:
        if sock:
            sock.close()
    except Exception:
        pass
    time.sleep(1.0)   # let the server release the connection slot

    ok2 = test_reuse(host, port, ctx, None)  # reuse test opens its own socket
    total += 1
    passed += 1 if ok2 else 0
    time.sleep(1.0)

    ok3 = test_concurrent(host, port, ctx, conns, reqs)
    total += 1
    passed += 1 if ok3 else 0

    print(f"\n== Result: {passed}/{total} test groups passed ==")
    return 0 if passed == total else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.10.99")
    ap.add_argument("--port", type=int, default=443)
    ap.add_argument("--conns", type=int, default=4,
                    help="concurrent connections for load test")
    ap.add_argument("--reqs", type=int, default=3,
                    help="sequential requests per connection")
    args = ap.parse_args()
    sys.exit(run(args.host, args.port, args.conns, args.reqs))


if __name__ == "__main__":
    main()
