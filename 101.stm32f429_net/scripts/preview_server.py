#!/usr/bin/env python3
"""Local preview server for web/dist with a mock of the device API.

Serves the built Vue app and fakes /api/* so all three views show data and
the LED/BEEP buttons work during UI review. Not used by the firmware.
"""
import json
import mimetypes
import os
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer

DIST = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), '..', 'web', 'dist'))

STATE = {
    'led': 0,
    'beep': 0,
}

HARDWARE = {
    'mcu': 'STM32F429IGT6',
    'clock': '180 MHz',
    'ap3216c': {'lux': 326, 'ps': 12, 'ir': 145},
    'mpu9250': {'ax': 0.02, 'ay': -0.01, 'az': 1.00,
                'gx': 0.1, 'gy': -0.2, 'gz': 0.0,
                'mx': 28.4, 'my': -12.1, 'mz': 41.7},
    'led': 0,
    'beep': 0,
}

NETWORK = {
    'ip': '192.168.10.99',
    'mask': '255.255.255.0',
    'gw': '192.168.10.1',
    'mac': '00:80:E1:42:10:99',
}


class Handler(BaseHTTPRequestHandler):
    def _json(self, obj, code=200):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _file(self, path):
        if path in ('', '/'):
            path = '/index.html'
        full = os.path.normpath(os.path.join(DIST, path.lstrip('/')))
        if not full.startswith(DIST):
            self.send_error(403)
            return
        if not os.path.isfile(full):
            self.send_error(404)
            return
        ctype = mimetypes.guess_type(full)[0] or 'application/octet-stream'
        with open(full, 'rb') as f:
            data = f.read()
        self.send_response(200)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path.startswith('/api/'):
            if self.path.startswith('/api/hardware'):
                HARDWARE['led'] = STATE['led']
                HARDWARE['beep'] = STATE['beep']
                self._json(HARDWARE)
            elif self.path.startswith('/api/network'):
                self._json(NETWORK)
            else:
                self._json({'error': 'unknown api'}, 404)
        else:
            self._file(self.path)

    def do_POST(self):
        try:
            n = int(self.headers.get('Content-Length', 0))
            raw = self.rfile.read(n) if n else b'{}'
            data = json.loads(raw) if raw else {}
        except Exception:
            data = {}
        if self.path.startswith('/api/control'):
            if 'led' in data:
                STATE['led'] = 1 if data['led'] else 0
            if 'beep' in data:
                STATE['beep'] = 1 if data['beep'] else 0
            self._json({'ok': True})
        elif self.path.startswith('/api/network'):
            for k in ('ip', 'mask', 'gw', 'mac'):
                if k in data and data[k]:
                    NETWORK[k] = data[k]
            self._json({'ok': True})
        elif self.path.startswith('/api/reset'):
            self._json({'ok': True})
        else:
            self._json({'error': 'unknown api'}, 404)

    def log_message(self, *a):
        pass


if __name__ == '__main__':
    srv = HTTPServer(('0.0.0.0', 8080), Handler)
    print('preview: http://localhost:8080/')
    srv.serve_forever()
