#!/usr/bin/env python3
"""仅供链接测试扩展回归测试使用的最小本地 HTTP 服务。"""

from http.server import BaseHTTPRequestHandler, HTTPServer


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        payload = b"ok"
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, format, *args):
        return


HTTPServer(("127.0.0.1", 18081), Handler).serve_forever()
