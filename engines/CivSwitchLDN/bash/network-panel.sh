#!/usr/bin/env bash
# IZ2S adaptation, 2026-11-08: release only the chosen radio. Ethernet and
# other NetworkManager connections stay managed throughout LDN delivery.
set -uo pipefail
[[ $# -ge 4 && "$1" == --phy ]] || exit 2
phy="$2"; helper="$3"; shift 3
[[ "$phy" =~ ^phy[0-9]+$ && -d "/sys/class/ieee80211/$phy" && -x "$helper" ]] || exit 2
temporary=iz2s-civ7-ldn
network_helper="$(dirname -- "${BASH_SOURCE[0]}")/../ldn/panel_network.py"
# Do not interfere with another live client using our reserved interface name.
if [[ -e "/sys/class/net/$temporary" ]]; then
    echo '[net] A previous LDN interface still exists. Restore Wi-Fi before starting another operation.' >&2
    exit 1
fi
interfaces=(); connections=(); managed=(); was_up=(); changed=()
for radio in /sys/class/net/*/phy80211; do
    [[ "$(basename -- "$(readlink -f -- "$radio")")" == "$phy" ]] || continue
    device="$(basename -- "$(dirname -- "$radio")")"
    interfaces+=("$device")
    connections+=("$(nmcli -g GENERAL.CON-UUID device show "$device" 2>/dev/null || true)")
    managed+=("$(LC_ALL=C nmcli -g GENERAL.NM-MANAGED device show "$device" 2>/dev/null || true)")
    flags="$(cat "/sys/class/net/$device/flags")"; was_up+=("$((flags & 1))")
done
link() {
    "$helper" -c '
import fcntl, socket, struct, sys
name, up = sys.argv[1].encode(), sys.argv[2] == "up"
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
    flags = struct.unpack("16sH14x", fcntl.ioctl(s, 0x8913, struct.pack("16sH14x", name, 0)))[1]
    flags = flags | 1 if up else flags & ~1
    fcntl.ioctl(s, 0x8914, struct.pack("16sH14x", name, flags))
' "$1" "$2"
}
restore_connection() {
    local device="$1" conn="$2" round probe state current address wait deadline=$((SECONDS + 120))
    for round in 1 2; do
        echo "[net] Waiting for Wi-Fi adapter $device (attempt $round/2)..."
        # Returning an interface to NM is asynchronous. Do not activate its
        # profile while the driver still reports unavailable/unmanaged.
        for (( probe=0; probe<15 && SECONDS<deadline; probe++ )); do
            state="$(LC_ALL=C nmcli -g GENERAL.STATE device show "$device" 2>/dev/null || true)"
            if [[ "$state" =~ ^(30|40|50|60|70|80|90|100)([^0-9]|$) ]]; then break; fi
            sleep 1
        done
        wait=$((deadline - SECONDS)); (( wait > 45 )) && wait=45
        (( wait > 0 )) || return 1
        nmcli --wait "$wait" connection up uuid "$conn" ifname "$device" || true
        # An activation result alone is insufficient: wait for this exact
        # profile to be connected and have an IPv4 address before probing TCP.
        for (( probe=0; probe<5 && SECONDS<deadline; probe++ )); do
            state="$(LC_ALL=C nmcli -g GENERAL.STATE device show "$device" 2>/dev/null || true)"
            current="$(nmcli -g GENERAL.CON-UUID device show "$device" 2>/dev/null || true)"
            address="$(nmcli -g IP4.ADDRESS device show "$device" 2>/dev/null || true)"
            if [[ "$state" =~ ^100([^0-9]|$) && "$current" == "$conn" && -n "$address" ]]; then return 0; fi
            sleep 1
        done
        if [[ "$round" == 1 ]]; then echo '[net] Wi-Fi is not ready yet; retrying the saved connection.'; fi
    done
    return 1
}
restore() {
    local status=$? failed=0 i device conn probe linked
    trap - EXIT INT TERM
    echo '[net] Restoring the selected Wi-Fi radio; Ethernet was kept connected.'
    # The child has exited. Delete its leftover VIF before raising wlan0; some
    # drivers reject raising the original interface while a station VIF exists.
    if [[ -e "/sys/class/net/$temporary" ]]; then
        nmcli device set "$temporary" managed no >/dev/null 2>&1 || true
        "$helper" "$network_helper" delete --phy "$phy" --ifname "$temporary" || failed=1
    fi
    for i in "${changed[@]}"; do
        device="${interfaces[i]}"; conn="${connections[i]}"
        if [[ "${was_up[i]}" == 1 ]]; then
            linked=0
            for (( probe=0; probe<5; probe++ )); do
                if link "$device" up; then linked=1; break; fi
                sleep 1
            done
            if (( ! linked )) && [[ "${managed[i]}" != yes || -z "$conn" || "$conn" == -- ]]; then failed=1; fi
        else link "$device" down || failed=1; fi
        if [[ "${managed[i]}" == yes ]]; then
            nmcli device set "$device" managed yes || failed=1
            if [[ -n "$conn" && "$conn" != -- ]]; then
                restore_connection "$device" "$conn" || failed=1
            else
                nmcli --wait 10 device disconnect "$device" >/dev/null 2>&1 || true
            fi
        fi
    done
    if (( failed )); then
        echo '[net] Wi-Fi restoration failed. Reconnect this radio manually before retrying.' >&2
        (( status == 0 )) && status=1
    else echo 'CIV7_LDN_NETWORK_RESTORED'; fi
    exit "$status"
}
child=''
interrupt() {
    if [[ -n "$child" ]]; then kill -TERM "$child" 2>/dev/null || true; wait "$child" 2>/dev/null || true; fi
    exit "$1"
}
trap restore EXIT
trap 'interrupt 130' INT
trap 'interrupt 143' TERM
for i in "${!interfaces[@]}"; do
    changed+=("$i")
    device="${interfaces[i]}"
    if [[ "${managed[i]}" == yes ]]; then nmcli device set "$device" managed no || exit 1; fi
    link "$device" down || exit 1
done
echo '[net] Selected Wi-Fi radio released for LDN. Ethernet remains connected.'
"$@" <&0 &
child=$!
wait "$child"
exit $?
