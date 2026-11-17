#!/usr/bin/env python3
"""Run the Http lifetime cases against a disposable loopback HTTP server."""
import http.server
import os
import subprocess
import sys
import threading
import time


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        slow = self.path == '/slow'
        body = b'x' * 4096 if slow else b'ok'
        count = 40 if slow else 1
        self.send_response(503 if self.path == '/error' else 200)
        self.send_header('Content-Length', str(len(body) * count))
        self.end_headers()
        try:
            for _ in range(count):
                self.wfile.write(body)
                self.wfile.flush()
                if slow:
                    time.sleep(0.025)
        except (BrokenPipeError, ConnectionResetError):
            pass  # Expected when a test cancels a transfer.

    def log_message(self, *_):
        pass


def main():
    with http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler) as server:
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        env = dict(os.environ, BAMBU_TEST_HTTP_URL=f'http://127.0.0.1:{server.server_port}')
        # Test traffic must stay local even when the developer uses an HTTP proxy.
        env['NO_PROXY'] = env['no_proxy'] = '127.0.0.1,localhost'
        try:
            return subprocess.run([sys.argv[1], '[HttpLifetime]'], env=env, timeout=60).returncode
        finally:
            server.shutdown()
            worker.join()


if __name__ == '__main__':
    raise SystemExit(main())
