#!/usr/bin/env python3
"""Loopback-only HTTPS release fixture for disposable Linux GUI environments.

Supply a test certificate for github.com and api.github.com. DNS/hosts and trust
must be configured inside the test container only; this server does not alter
them. Never add its certificate to a personal machine's trust store.
"""
import argparse
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import ssl
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('payload', type=Path)
    parser.add_argument('--certificate', required=True, type=Path)
    parser.add_argument('--key', required=True, type=Path)
    parser.add_argument('--mode-file', required=True, type=Path)
    parser.add_argument('--repository', required=True, help='Repository configured in the test build')
    parser.add_argument('--version', default='02.09.00.01')
    parser.add_argument('--port', type=int, default=443)
    options = parser.parse_args()
    payload = options.payload.resolve(strict=True)
    digest = hashlib.sha256()
    with payload.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    asset = 'BambuStudio_linux-x86_64.AppImage'
    release = {'tag_name': 'v' + options.version, 'body': 'Isolated crossplat update fixture',
               'draft': False, 'prerelease': False,
               'assets': [{'name': asset, 'size': payload.stat().st_size,
                           'digest': 'sha256:' + digest.hexdigest(),
                           'browser_download_url': 'https://github.com/' + options.repository +
                           '/releases/download/v' + options.version + '/' + asset}]}

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            mode = options.mode_file.read_text().strip() if options.mode_file.exists() else 'normal'
            if mode in ('offline', 'rate-limit'):
                self.send_error(503 if mode == 'offline' else 403)
                return
            if self.path == '/repos/' + options.repository + '/releases?per_page=100':
                body = json.dumps([release]).encode()
                self.send_response(200)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            elif self.path == '/'+options.repository+'/releases/download/v'+options.version+'/'+asset:
                self.send_response(200)
                self.send_header('Content-Length', str(payload.stat().st_size))
                self.end_headers()
                try:
                    with payload.open('rb') as stream:
                        first = True
                        while chunk := stream.read(65536):
                            if mode == 'corrupt' and first:
                                chunk = bytes([chunk[0] ^ 1]) + chunk[1:]
                            first = False
                            self.wfile.write(chunk)
                            self.wfile.flush()
                            if mode == 'slow':
                                time.sleep(.03)
                except (BrokenPipeError, ConnectionResetError):
                    pass
            else:
                self.send_error(404)

    server = ThreadingHTTPServer(('127.0.0.1', options.port), Handler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(options.certificate, options.key)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    print(f'Fixture ready on loopback:{options.port}; SHA-256 {digest.hexdigest()}', flush=True)
    try:
        server.serve_forever()
    finally:
        server.server_close()


if __name__ == '__main__':
    main()
