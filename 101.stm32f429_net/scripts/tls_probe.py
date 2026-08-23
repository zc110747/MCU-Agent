import socket, ssl, sys

try:
    s = socket.create_connection(("192.168.10.99", 443), timeout=5)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    ss = ctx.wrap_socket(s, server_hostname="stm32f429.local")
    print("handshake OK, version:", ss.version())
    ss.sendall(b"GET / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
    data = ss.recv(300)
    print("recv:", data[:60])
    ss.close()
except Exception as e:
    print("FAIL:", repr(e))
