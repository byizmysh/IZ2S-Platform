"""Discover a host and keep LDN connected throughout chat delivery."""

import math

import ldn
import trio

import connection
import lobby

CONNECT_TIMEOUT = 45


async def run(args):
    """Scan first, then keep the selected LDN connection open for the lobby."""
    keys = connection.load_keys(args.keys)
    found = await connection.scan(
        keys,
        phy=args.phy,
        ifname=args.ifname,
        host=args.host,
    )

    # Auto-selection is safe only when exactly one network was found.
    if args.host is None:
        if not found:
            raise RuntimeError("No LDN network found. Open the game's wireless lobby and try again.")
        if len(found) != 1:
            raise RuntimeError(f"Multiple LDN networks found: {', '.join(found)}. Select one with --host MAC.")
        args.host = next(iter(found))

    if args.host not in found:
        raise RuntimeError("Host not found. Check MAC, channels, keys.")

    info = found[args.host]
    param = connection.build_connect_params(
        info,
        keys,
        phy=args.phy,
        ifname=args.ifname,
    )

    print(f"Connecting via LDN to {args.host} (channel {info.channel})...", flush=True)

    try:
        with trio.fail_after(CONNECT_TIMEOUT) as timeout:
            async with ldn.connect(param) as network_obj:
                # The deadline covers association, not the chat session.
                timeout.deadline = math.inf
                print("LDN connected. Joining the Civilization VII lobby...", flush=True)
                return await lobby.run(args, network_obj)
    except trio.TooSlowError as e:
        raise RuntimeError("LDN connection timed out. Check that the wireless lobby is still open.") from e


