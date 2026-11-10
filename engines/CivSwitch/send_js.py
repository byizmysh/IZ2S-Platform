#!/usr/bin/env python3
"""send_js.py - send a JS payload to the Remote JS Loader and print the log
output it streams back.

Usage:
  python send_js.py <console-ip> [port] file.js

The payload is sent as-is; if it uses top-level await, wrap it in your own
async IIFE. The loader reads until the client
half-closes, so we shutdown(WR) after sending, then read until close/timeout.
"""
import socket
import sys

TIMEOUT = 120


def send(host, port, code, *, timeout=TIMEOUT):
    s = socket.create_connection((host, port), timeout=8)
    s.sendall(code.encode())
    s.shutdown(socket.SHUT_WR)
    s.settimeout(timeout)
    data = b""
    try:
        while True:
            chunk = s.recv(65536)
            if not chunk:
                break
            data += chunk
    except socket.timeout:
        pass
    s.close()
    return data.decode("utf-8", "replace")


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        sys.exit(1)
    host = args[0]
    port = 9026
    rest = args[1:]
    if rest and rest[0].isdigit():
        port = int(rest[0])
        rest = rest[1:]
    if not rest:
        print(__doc__)
        sys.exit(1)
    code = open(rest[0], encoding="utf-8").read()
    print(send(host, port, code))


if __name__ == "__main__":
    main()
