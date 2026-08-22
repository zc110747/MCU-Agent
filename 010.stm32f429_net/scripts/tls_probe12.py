import socket, ssl
s = socket.create_connection(("192.168.10.99", 443), timeout=5)
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE
ctx.minimum_version = ssl.TLSVersion.TLSv1_2
ctx.maximum_version = ssl.TLSVersion.TLSv1_2
ss = ctx.wrap_socket(s, server_hostname="stm32f429.local")
print("handshake OK:", ss.version())
ss.sendall(b"GET / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
print("recv:", ss.recv(80)[:50])
ss.close()
