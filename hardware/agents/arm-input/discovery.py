"""IZlauncher LAN discovery: public metadata only; no keyboard or command API."""
import argparse
import fcntl
import hashlib
import ipaddress
import json
import platform
import re
import socket
import struct
import time
from pathlib import Path

PORT = 39418
GROUP = '239.255.77.14'
PROTOCOL = 'izlauncher.discovery'


def interfaces():
    rows = []
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        # Read sysfs instead of opening AF_NETLINK, which the service disallows.
        for interface in Path('/sys/class/net').iterdir():
            name = interface.name
            if name == 'lo' or re.match(r'^(docker|podman|veth|virbr|vnet|vmnet|lxc|br-|tun|wg|tailscale)', name):
                continue
            try:
                raw = fcntl.ioctl(sock.fileno(), 0x8915, struct.pack('256s', name.encode()[:15]))
                rows.append({'address': socket.inet_ntoa(raw[20:24]),
                             'mode': 'wifi' if Path('/sys/class/net', name, 'wireless').exists() else 'lan'})
            except OSError:
                pass
    return rows


def reply(raw, identity, name, rows):
    if len(raw) > 512:
        return None
    try:
        value = json.loads(raw)
        if (not isinstance(value, dict) or value.get('protocol') != PROTOCOL or
                value.get('version') != 1 or value.get('kind') != 'discover' or
                not re.fullmatch(r'[a-f0-9]{32}', value.get('nonce', ''))):
            return None
        return json.dumps({'protocol': PROTOCOL, 'version': 1, 'kind': 'input',
                           'nonce': value['nonce'], 'id': identity, 'name': name,
                           'architecture': platform.machine(), 'interfaces': rows[:8],
                           'capabilities': ['usb-keyboard']}).encode()
    except (ValueError, TypeError):
        return None


def serve(port=PORT, bind='0.0.0.0'):
    identity_file = Path('/etc/machine-id')
    seed = identity_file.read_text().strip() if identity_file.exists() else socket.gethostname()
    identity = hashlib.sha256(seed.encode()).hexdigest()[:32]
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.IPPROTO_IP, 8, 1)  # Linux IP_PKTINFO: reply through the incoming interface.
    sock.bind((bind, port))
    for row in interfaces():
        try:
            sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
                            socket.inet_aton(GROUP) + socket.inet_aton(row['address']))
        except OSError:
            pass
    print(json.dumps({'event': 'ready', 'port': sock.getsockname()[1]}), flush=True)
    seen = {}
    while True:
        raw, ancillary, _, remote = sock.recvmsg(513, 128)
        address = ipaddress.ip_address(remote[0])
        if not any(address in ipaddress.ip_network(cidr) for cidr in ('10.0.0.0/8', '172.16.0.0/12', '192.168.0.0/16')):
            continue
        now = time.monotonic()
        if now - seen.get(remote[0], -10) < .2:
            continue
        seen = {key: value for key, value in seen.items() if now - value < 5}
        if len(seen) >= 64:
            continue
        seen[remote[0]] = now
        response = reply(raw, identity, socket.gethostname(), interfaces())
        if response:
            packet_info = [(level, kind, data) for level, kind, data in ancillary
                           if level == socket.IPPROTO_IP and kind == 8]
            try:
                sock.sendmsg([response], packet_info, 0, remote)
            except OSError:
                pass


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, default=PORT)
    parser.add_argument('--bind', default='0.0.0.0')
    args = parser.parse_args()
    serve(args.port, args.bind)
