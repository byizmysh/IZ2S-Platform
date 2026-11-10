"""Arguments passed by run.js to the LDN injection client."""

import argparse
from pathlib import Path
import re


def build_parser():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--keys", type=Path, default="prod.keys")
    cli.add_argument("--phy", required=True, help="Wi-Fi radio selected by run.js")
    cli.add_argument("--ifname", default="civ7-ldn")
    cli.add_argument("--verbose", action="store_true", help="show packet and diagnostic logs")
    cli.add_argument(
        "--host",
        type=_mac,
        help="host MAC; auto-selected when only one network is found",
    )
    cli.add_argument(
        "--chat-delay",
        type=int,
        default=10,
        dest="chat_delay_ms",
        metavar="MS",
        help="delay between chat messages in milliseconds (default: 10)",
    )
    cli.add_argument(
        "--handshake-scale",
        type=float,
        default=1.0,
        dest="handshake_scale",
        metavar="N",
        help="multiply handshake delays by N (default: 1.0, e.g. 3 for slower networks)",
    )
    return cli


def _mac(value):
    if not re.fullmatch(r"(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}", value):
        raise argparse.ArgumentTypeError("MAC expected, e.g. aa:bb:cc:dd:ee:ff")
    return value.lower()
