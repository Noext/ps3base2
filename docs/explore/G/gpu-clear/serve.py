#!/usr/bin/env python3
import http.server, socketserver, sys
PORT = int(sys.argv[1]); ROOT = sys.argv[2]
class H(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **kw):
        super().__init__(*a, directory=ROOT, **kw)
    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()
    def log_message(self, f, *a): sys.stderr.write('%s - %s\n' % (self.address_string(), f % a))
socketserver.TCPServer.allow_reuse_address = True
with socketserver.TCPServer(('127.0.0.1', PORT), H) as s:
    print('serving', ROOT, PORT, flush=True)
    s.serve_forever()
