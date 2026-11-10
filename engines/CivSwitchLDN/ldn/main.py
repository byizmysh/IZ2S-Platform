#!/usr/bin/env python3
"""Scan LDN networks, connect to a host and send messages in a Civ VII lobby."""

import importlib
import logging
import signal
import sys

import connection
from cli import build_parser


def error_details(error):
    """Expose the actual association error inside a Trio exception group."""
    nested = getattr(error, "exceptions", ())
    if nested:
        return "; ".join(error_details(item) for item in nested)
    return f"{type(error).__name__}: {error}"


def main(argv=None):
    cli = build_parser()
    args = cli.parse_args(argv)

    errors = connection.hardware_check(args.phy, args.ifname)
    if errors:
        for e in errors:
            print(f"[!] {e}", file=sys.stderr)
        return 1

    # Report missing packages before attempting the LDN scan.
    try:
        importlib.import_module("ldn")
        importlib.import_module("trio")
    except ImportError as e:
        print(
            f"Missing dependency: {e.name}. Run: pip install -r ldn/requirements.txt",
            file=sys.stderr,
        )
        return 1

    connection.install_ip_fallback()
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.WARNING,
        format="%(levelname)s %(name)s: %(message)s",
    )

    import session
    import panel_network
    panel_network.install_interface_guard(args.phy, args.ifname)
    trio = importlib.import_module("trio")
    signal.signal(signal.SIGINT, interrupt)
    signal.signal(signal.SIGTERM, interrupt)

    try:
        return trio.run(session.run, args)
    except KeyboardInterrupt:
        print("\nInterrupted.")
        return 130
    except Exception as e:
        print(f"Error: {error_details(e)}", file=sys.stderr)
        return 1


def interrupt(signum, frame):
    raise KeyboardInterrupt


if __name__ == "__main__":
    sys.exit(main())
