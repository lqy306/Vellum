#!/usr/bin/env python3
"""仅供本地 AI 插件测试使用的最小 OpenAI 兼容 HTTP 服务。"""

from http.server import BaseHTTPRequestHandler, HTTPServer


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        self.rfile.read(length)
        payload = b'{"choices":[{"message":{"content":" return 0;\\n}"}}]}'
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, format, *args):
        return


HTTPServer(("127.0.0.1", 18080), Handler).serve_forever()
