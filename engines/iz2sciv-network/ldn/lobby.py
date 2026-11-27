"""Civ VII lobby handshake and chat delivery, running on Trio."""

from collections import deque
import logging
from pathlib import Path
import socket
import struct
import sys
import time

import trio

import protocol
import transport
from connection import PLAYER_NAME

DEFAULT_CHAT_DELAY_MS = 10
DEFAULT_HANDSHAKE_SCALE = 1.0
MIN_DURATION = 30
logger = logging.getLogger(__name__)


async def run(args, network):
    """Send the lobby handshake and chat while the LDN link is open."""
    host_ip = str(network.info().participants[0].ip_address)
    local_ip = str(network.participant().ip_address)
    chat_delay = getattr(args, "chat_delay_ms", DEFAULT_CHAT_DELAY_MS) / 1000
    hs_scale = getattr(args, "handshake_scale", DEFAULT_HANDSHAKE_SCALE)
    session = LobbySession(local_ip, host_ip, read_chat_lines(), chat_delay, hs_scale)

    await warmup_arp(args.ifname, local_ip, host_ip)

    # Bind game traffic to LDN so the normal network cannot receive it.
    with trio.socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BINDTODEVICE, args.ifname.encode() + b"\0")
        await sock.bind((local_ip, protocol.PORT))
        return await session.run(sock)


class LobbySession:
    """Own the handshake state, transport and scheduled chat messages."""

    HANDSHAKE_DELAYS = (0.1, 0.2, 0.4)
    CHAT_START_DELAY = 0.5

    def __init__(self, local_ip, host_ip, chat_lines, chat_delay=DEFAULT_CHAT_DELAY_MS / 1000, hs_scale=DEFAULT_HANDSHAKE_SCALE):
        self.host_ip = host_ip
        self.chat_lines = chat_lines
        self.chat_delay = chat_delay
        self.hs_scale = hs_scale

        # FireCONN carries the game's introduction inside an LDN F packet.
        intro = protocol.introduction(identity=PLAYER_NAME, name=PLAYER_NAME)
        body = protocol.connection_payload(host_ip, intro)
        self.initial = protocol.ip_bytes(host_ip) + b"F" + body
        self.transport = transport.Transport(local_ip, host_ip, self.initial)

        self.follow = deque()
        self.last_sent = None
        self.joined = False
        self.received = 0
        self.chat_sent = 0
        self.progress_open = False
        self.t0 = None

    async def run(self, sock):
        try:
            return await self.deliver(sock)
        finally:
            self.finish_progress()

    def elapsed(self):
        if self.t0 is None:
            return 0.0
        return time.monotonic() - self.t0

    async def deliver(self, sock):
        self.t0 = time.monotonic()
        chat_start = self.CHAT_START_DELAY * self.hs_scale
        duration = max(MIN_DURATION, 15 + chat_start + self.chat_delay * len(self.chat_lines))
        start = next_attempt = time.monotonic()
        attempts = 0

        while time.monotonic() - start < duration:
            now = time.monotonic()

            # Retry the introduction until the host acknowledges the join.
            if not self.joined and attempts < 3 and now >= next_attempt:
                attempts += 1
                await self.send(sock, self.initial, f"FireCONN attempt {attempts}/3")
                next_attempt = now + 3.0

            if not await self.receive(sock):
                return 2

            await self.send_pending(sock)

            if (
                self.last_sent is not None
                and not self.follow
                and time.monotonic() - self.last_sent >= 1.0 * self.hs_scale
            ):
                break
        if not self.joined or self.follow or self.last_sent is None:
            self.finish_progress()
            message = "Payload delivery did not complete." if self.joined else "Timed out joining the game lobby."
            print(f"Error: {message}", file=sys.stderr, flush=True)
            return 3
        logger.debug("[+%.3fs] Session complete: %s game messages received from host", self.elapsed(), self.received)
        print("Payload fully sent over LDN. Closing the wireless connection...", flush=True)
        return 0

    async def receive(self, sock):
        """Process one datagram. Return False if the host refuses or closes."""
        data = None
        with trio.move_on_after(0.1):
            data, addr = await sock.recvfrom(65535)

        if data is None or addr != (self.host_ip, protocol.PORT):
            return True

        # The transport handles ordering and retransmissions before game parsing.
        bodies, responses, detail = self.transport.receive(data)
        logger.debug("[+%.3fs] RX %s", self.elapsed(), detail)

        for response in responses:
            await self.send(sock, response, "transport response")

        for body in bodies:
            self.received += 1
            description = protocol.describe(body)
            logger.debug("[+%.3fs] RX game: %s (%sB)", self.elapsed(), description, len(body))

            refused = body.startswith((protocol.REFUSED, protocol.CLOSE))
            rejected = description.startswith(("NetJoinRefused", "NetKicked"))
            if refused or rejected:
                self.finish_progress()
                print("Error: The host refused the join request or closed the lobby.", file=sys.stderr, flush=True)
                return False

            welcome = body.startswith(protocol.REPLY) or description.startswith("NetWelcome")
            if not self.joined and welcome:
                self.schedule_messages(body)

        return True

    async def send_pending(self, sock):
        # Transport sequence numbers are assigned when a message is sent.
        while self.follow and time.monotonic() >= self.follow[0][0]:
            _, name, build = self.follow.popleft()
            packet = self.transport.queue(build(len(self.transport.sent)))
            await self.send(sock, packet, f"{name} (seq {self.transport.next_seq - 1})")
            self.last_sent = time.monotonic()
            if name.startswith("NetChat["):
                self.chat_sent += 1
                self.show_progress()
            if not self.follow:
                print("Payload sent.", flush=True)

        for packet in self.transport.timers(time.monotonic()):
            await self.send(sock, packet, "keepalive R/V")

    def schedule_messages(self, welcome):
        slot = struct.unpack_from("<I", welcome, 40)[0] if len(welcome) >= 44 else 1
        logger.debug("[+%.3fs] Assigned lobby slot: %s", self.elapsed(), slot)
        print("Lobby joined.", flush=True)
        if self.chat_lines:
            self.show_progress()
        now = time.monotonic()

        d1, d2, d3 = (d * self.hs_scale for d in self.HANDSHAKE_DELAYS)
        chat_start = self.CHAT_START_DELAY * self.hs_scale

        # Register with the game before injecting any chat messages.
        self.follow.extend([
            (now + d1, "NetRegister", protocol.net_register),
            (now + d2, "NetAppRegister", lambda n: protocol.net_app_register(n, slot)),
            (now + d3, "NetSnapshotProcessed", protocol.net_snapshot_processed),
        ])
        logger.debug("[+%.3fs] Handshake scheduled at +%.0f/+%.0f/+%.0fms (scale %.1fx), chat at +%.0fms",
                     self.elapsed(), d1 * 1000, d2 * 1000, d3 * 1000, self.hs_scale, chat_start * 1000)

        for i, line in enumerate(self.chat_lines):
            self.follow.append((
                now + chat_start + self.chat_delay * i,
                f"NetChat[{i + 1}/{len(self.chat_lines)}]",
                lambda n, text=line: protocol.net_chat_public(n, text, slot),
            ))
        self.joined = True

    def show_progress(self):
        total = len(self.chat_lines)
        percent = self.chat_sent * 100 // total
        inline = sys.stdout.isatty() and not logger.isEnabledFor(logging.DEBUG)
        self.progress_open = inline and self.chat_sent < total
        print(
            f"{'\r' if inline else ''}Sending payload: {percent}% ({self.chat_sent}/{total} messages)",
            end="" if self.progress_open else "\n",
            flush=True,
        )

    def finish_progress(self):
        if self.progress_open:
            print(flush=True)
            self.progress_open = False

    async def send(self, sock, data, reason):
        await sock.sendto(data, (self.host_ip, protocol.PORT))
        logger.debug("[+%.3fs] TX %s (%sB)", self.elapsed(), reason, len(data))


def read_chat_lines():
    """Read the compressed chat lines supplied by run.js on stdin."""
    return [line for line in sys.stdin.read().splitlines() if line.strip()]


async def warmup_arp(device, local_ip, host_ip):
    """Prime address resolution before starting the UDP handshake."""
    try:
        address = Path(f"/sys/class/net/{device}/address").read_text().strip()
        mac = bytes.fromhex(address.replace(":", ""))
        frame = transport.arp_request(mac, local_ip, transport.neighbor_mac(device, host_ip), host_ip)

        with socket.socket(socket.AF_PACKET, socket.SOCK_RAW) as raw:
            raw.bind((device, 0))
            for _ in range(2):
                raw.send(frame)
                await trio.sleep(0.1)

        logger.debug("ARP warmup sent")
    except (OSError, ValueError) as e:
        logger.warning("Could not prepare LDN address resolution: %s", e)
    await trio.sleep(0.3)
