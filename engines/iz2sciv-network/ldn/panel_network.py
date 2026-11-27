"""IZ2S radio lifecycle: isolate temporary station and shield its cleanup."""
import contextlib
import pathlib
import socket
import sys


def install_interface_guard(phy, ifname, *, wlan=None, trio_module=None, run=None):
    if wlan is None:
        from ldn import wlan
    if trio_module is None:
        import trio as trio_module
    if run is None:
        run = trio_module.run_process
    original = wlan.Factory._create_interface
    if getattr(original, '_iz2s_guard', False):
        return

    @contextlib.asynccontextmanager
    async def guarded(factory, phyname, name, kind, extra=None):
        manager = original(factory, phyname, name, kind, **({'extra': extra} if extra is not None else {}))
        attributes = await manager.__aenter__()
        try:
            if phyname == phy and name == ifname and kind == wlan.nl80211.NL80211_IFTYPE_STATION:
                # NM sees station interfaces as ordinary Wi-Fi devices. Release
                # this one before the LDN library raises it or associates it.
                with trio_module.fail_after(20):
                    while True:
                        result = await run(['nmcli', '--wait', '2', 'device', 'set', name, 'managed', 'no'],
                                           check=False, capture_stdout=True, capture_stderr=True)
                        if result.returncode == 0:
                            state = await run(['nmcli', '-g', 'GENERAL.NM-MANAGED', 'device', 'show', name],
                                              check=False, capture_stdout=True, capture_stderr=True,
                                              env={**__import__('os').environ, 'LC_ALL': 'C'})
                            if state.returncode == 0 and state.stdout.strip() == b'no':
                                break
                        await trio_module.sleep(.1)
                print('[net] Temporary LDN interface isolated from NetworkManager.', flush=True)
            yield attributes
        finally:
            # Trio's association deadline cancels the surrounding scope. The
            # upstream async finalizer otherwise gets cancelled before DEL_IF.
            with trio_module.CancelScope(shield=True):
                with trio_module.fail_after(5):
                    await manager.__aexit__(None, None, None)

    guarded._iz2s_guard = True
    wlan.Factory._create_interface = guarded


async def remove_temporary(phy, name):
    from netlink import nl80211
    if name != 'iz2s-civ7-ldn' or not phy.startswith('phy') or not phy[3:].isdigit():
        raise ValueError('Invalid IZ2S temporary radio interface')
    radio = pathlib.Path('/sys/class/net', name, 'phy80211')
    if not radio.exists():
        return
    if radio.resolve().name != phy:
        raise ValueError('Temporary interface belongs to another Wi-Fi radio')
    async with nl80211.connect() as net:
        await net.request(nl80211.NL80211_CMD_DEL_INTERFACE,
                          {nl80211.NL80211_ATTR_IFINDEX: socket.if_nametoindex(name)})
    print('[net] Removed the temporary LDN interface.', flush=True)


if __name__ == '__main__':
    import argparse
    import trio
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument('action', choices=['delete'])
    cli.add_argument('--phy', required=True)
    cli.add_argument('--ifname', required=True)
    args = cli.parse_args()
    trio.run(remove_temporary, args.phy, args.ifname)
