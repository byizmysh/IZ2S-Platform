"""UDP transport layer for Civ VII lobby communication.

Implements the F/R/V/SP packet framing, sequence numbering and retransmission
observed on the wire. Each packet starts with a 4-byte IPv4 destination prefix
followed by a 1-byte kind tag and an 8-byte sequence number.
"""

import socket
import struct
import time
from pathlib import Path

import protocol

SPECIAL = (1 << 64) - 1


class Transport:
    """Ordered reliable delivery over UDP with F/R/V sequencing."""

    def __init__(self, local_ip, host_ip, initial_packet):
        self.local_ip = local_ip
        self.host_ip = host_ip
        self.initial = initial_packet
        self.sent = {}

        if len(initial_packet) >= 13:
            self.next_seq = struct.unpack_from("<Q", initial_packet, 5)[0] + 1
        else:
            self.next_seq = 1

        self.expected = 0
        self.fresh = True
        self.pending = {}
        self.last_r = self.last_v = time.monotonic()

    def receive(self, data):
        """Process a received packet. Returns (payloads[], responses[], detail)."""
        if len(data) < 5:
            return [], [], "too short"

        # Civ VII stores the recipient's IPv4 bytes in reversed order.
        if data[:4] != protocol.ip_bytes(self.local_ip):
            return [], [], "wrong destination"

        kind, body = data[4:5], data[5:]

        # S frames are a small ping/pong exchange outside normal sequencing.
        if kind == b"S":
            if body == b"P":
                return [], [self._packet(b"S", b"p")], "SP→Sp"
            return [], [], "Sp" if body == b"p" else "unknown S-control"

        if kind not in (b"F", b"R", b"V") or len(body) < 8:
            return [], [], "invalid type/length"

        sequence = struct.unpack_from("<Q", body)[0]
        initial_seq = struct.unpack_from("<Q", self.initial, 5)[0]

        # R asks for a missing packet; V probes the most recent sent packet.
        if kind == b"R":
            found = self.initial if sequence == initial_seq else self.sent.get(sequence)
            return [], [found] if found else [], f"R({sequence})"

        if kind == b"V":
            last = self.next_seq - 1
            resend = initial_seq != SPECIAL and sequence <= last
            pkt = self.initial if last == initial_seq else self.sent.get(last, self.initial)
            return [], [pkt] if resend else [], f"V({sequence})"

        # F-packet
        payload = body[8:]
        if sequence == SPECIAL:
            return [payload], [], "F(special)"

        if sequence == 0:
            self.expected = 0
            self.pending.clear()
        elif self.fresh:
            self.expected = sequence

        self.fresh = False

        if sequence < self.expected:
            return [], [], f"F({sequence}) already received"
        if sequence > self.expected:
            # Buffer a small number of future packets until the gap arrives.
            if len(self.pending) < 64:
                self.pending.setdefault(sequence, payload)
            return [], [], f"F({sequence}) out of order"

        delivered = [payload]
        self.expected += 1
        while self.expected in self.pending:
            delivered.append(self.pending.pop(self.expected))
            self.expected += 1

        return delivered, [], f"F({sequence})"

    def queue(self, payload):
        """Build a numbered F-packet, kept for retransmission."""
        seq = self.next_seq
        self.next_seq += 1
        data = self._packet(b"F", struct.pack("<Q", seq) + payload)
        self.sent[seq] = data

        return data

    def timers(self, now):
        """Periodic R/V keepalive packets."""
        if self.pending and now - self.last_r > 0.5:
            self.last_r = now
            return [self._control(b"R", self.expected)]

        if now - self.last_v >= 5:
            self.last_v = now
            return [self._control(b"V", self.expected)]
        return []

    def _packet(self, kind, body):
        return protocol.ip_bytes(self.host_ip) + kind + body

    def _control(self, kind, sequence):
        return self._packet(kind, struct.pack("<Q", sequence))


def arp_request(src_mac, src_ip, dst_mac, dst_ip):
    """Build a raw Ethernet ARP request frame."""
    header = struct.pack(">HHBBH", 1, 0x0800, 6, 4, 1)
    return (
        dst_mac + src_mac + b"\x08\x06" + header
        + src_mac + socket.inet_aton(src_ip)
        + bytes(6) + socket.inet_aton(dst_ip)
    )


def neighbor_mac(device, address):
    """Look up a neighbor's MAC from /proc/net/arp."""
    try:
        for line in Path("/proc/net/arp").read_text().splitlines()[1:]:
            fields = line.split()
            if fields[0] == address and fields[5] == device and fields[3] != "00:00:00:00:00:00":
                return bytes.fromhex(fields[3].replace(":", ""))
    except (OSError, IndexError, ValueError):
        pass

    # An Ethernet broadcast still reaches the host when ARP has no entry yet.
    return b"\xff" * 6
