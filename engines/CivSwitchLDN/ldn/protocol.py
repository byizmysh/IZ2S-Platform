"""Civ VII game-level message codecs (NetWelcome, NetRegister, NetChat, etc.).

These ride inside the F/R/V transport frames defined in transport.py.
All multi-byte integers are little-endian.
"""

import ipaddress
import struct

# FireCONN/FireRPLY/FireRFUS/FireClose markers (8-byte LE magic)
CONN = struct.pack("<Q", 0x46697265434F4E4E)
REPLY = struct.pack("<Q", 0x4669726552504C59)
REFUSED = struct.pack("<Q", 0x4669726552465553)
CLOSE = struct.pack("<Q", 0x46697265B9133AB7)

# Game UDP port
PORT = 62056

# Known message types
KNOWN = {
    0x4B0: "NetWelcome",
    0x4BA: "NetJoinRefused",
    0x4C2: "NetPeerInfo",
    0x4B8: "NetRegister",
    0x4B9: "NetAppRegister",
    0x4E9: "NetSnapshotProcessed",
    0x4B6: "NetIntroduction",
    0x3F3: "NetChat",
    0x3EC: "NetKicked",
    0x4B3: "NetPeerFailure",
}

REFUSAL_REASONS = {
    0: "generic error",
    6: "table full",
    0x0E: "invalid token",
    0x0F: "empty name",
}


def introduction(identity="Civ7-PC", name="Civ7-PC", intro_id=0, field84=6, flag=0, extra=""):
    """Build the game-level introduction carried by FireCONN."""
    extra_bytes = extra.encode("utf-8")
    return (
        struct.pack("<I", intro_id)
        + _text_block(identity)
        + _text_block(name)
        + struct.pack("<IBI", field84, flag, len(extra_bytes))
        + extra_bytes
    )


def connection_payload(host_ip, local_intro, sequence=0):
    """Build the initial FireCONN packet content."""
    # The FireCONN marker is followed by the host IP in an eight-byte field.
    prefix = struct.pack("<Q", int.from_bytes(ip_bytes(host_ip), "little"))
    payload = CONN + prefix + bytes(4) + local_intro
    return struct.pack("<Q", sequence) + payload


def net_register(counter, version="1.0.0"):
    data = version.encode("utf-8")
    return _message(0x4B8, counter, struct.pack("<I", len(data)) + data)


def net_app_register(counter, slot=1, is_host=0):
    return _message(0x4B9, counter, struct.pack("<IB", slot, is_host))


def net_snapshot_processed(counter):
    return _message(0x4E9, counter, struct.pack("<IB", 0, 1))


def net_chat_public(counter, text, sender=0):
    """Public chat accepted by the host. sender=0 shows as host, 1+ as that slot."""
    return net_chat(counter, text, sender, chat_type=-2, target=-1)


def net_chat(counter, text, recipient=-1, chat_type=0, target=-1):
    data = text.encode("utf-8")
    fields = struct.pack("<iI", recipient, len(data))
    tail = struct.pack("<ii", chat_type, target)

    return _message(
        0x3F3,
        counter,
        fields + data + tail,
    )


def describe(payload):
    for marker, label in (
        (CONN, "FireCONN"),
        (REPLY, "FireRPLY"),
        (REFUSED, "FireRFUS"),
        (CLOSE, "FireClose"),
    ):
        if payload.startswith(marker):
            return label

    if len(payload) >= 28:
        header = struct.unpack_from("<7I", payload)
        if header[1] == 1:
            label = KNOWN.get(header[0], "message")

            if header[0] == 0x4BA and len(payload) >= 32:
                reason = struct.unpack_from("<I", payload, 28)[0]
                meaning = REFUSAL_REASONS.get(reason, "unknown")
                return f"NetJoinRefused (reason=0x{reason:x}: {meaning})"

            return f"{label} (type=0x{header[0]:x})"

    return f"unknown ({len(payload)} bytes)"


def ip_bytes(address):
    """Encode an IPv4 address in the reversed byte order used by Civ VII."""
    return ipaddress.IPv4Address(address).packed[::-1]


def _text_block(text, allow_empty=False):
    data = text.encode("utf-8")
    if (not data and not allow_empty) or len(data) > 63 or b"\0" in data:
        raise ValueError("names must be 1-63 UTF-8 bytes without NUL")

    return data.ljust(64, b"\0")


def _message(kind, counter, body):
    """Common 7×u32 LE header: type, 1, 1, -1, -1, 1, counter."""
    return struct.pack("<7I", kind, 1, 1, 0xFFFFFFFF, 0xFFFFFFFF, 1, counter) + body
