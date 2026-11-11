#!/usr/bin/env python3
"""Serve the page with the headers the browser requires. Milestone 4.

Two sets of headers matter, and neither is optional:

  Cross-Origin-Opener-Policy: same-origin
  Cross-Origin-Embedder-Policy: require-corp

Without them SharedArrayBuffer is undefined, and the AudioWorklet and the command
ring both depend on it -- the page would load and then fail at the first Atomics
call with no obvious cause. python3 -m http.server does not send them, which is
why plain http.server is not a valid way to test this page.

WebGPU also needs a secure context. 127.0.0.1 counts as one, so no TLS is needed
for local testing.

usage: python3 web/serve.py [port]
"""
import http.server
import os
import socketserver
import sys

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8791
ROOT = os.path.dirname(os.path.abspath(__file__))


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **kw):
        super().__init__(*a, directory=ROOT, **kw)

    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        self.send_header('Accept-Ranges', 'bytes')
        super().end_headers()

    def log_message(self, fmt, *args):
        sys.stderr.write('%s - %s\n' % (self.address_string(), fmt % args))


with socketserver.TCPServer(('127.0.0.1', PORT), Handler) as httpd:
    httpd.allow_reuse_address = True
    print('serving %s at http://127.0.0.1:%d/game.html' % (ROOT, PORT))
    print('cross-origin isolated: SharedArrayBuffer will be available')
    httpd.serve_forever()