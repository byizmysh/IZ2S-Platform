#!/usr/bin/env bash
# IZ2S adaptation, 2026-11-08: release only the chosen radio. Ethernet and
# other NetworkManager connections stay managed throughout LDN delivery.
set -uo pipefail
mode=send
if [[ "${1:-}" == --restore ]]; then
    [[ $# == 2 ]] || exit 2
    mode=restore; helper="$2"; phy=''; shift 2
else
    [[ $# -ge 4 && "$1" == --phy ]] || exit 2
    phy="$2"; helper="$3"; shift 3
fi
[[ -x "$helper" ]] || exit 2
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
sys_root="${IZ2S_NETWORK_SYSFS:-/sys}"
state_file="${IZ2S_NETWORK_STATE:-$script_dir/../../../data/state/civ7-network.json}"
state_helper="$script_dir/../ldn/network_state.py"
network_helper="$script_dir/../ldn/panel_network.py"
temporary=iz2s-civ7-ldn
mkdir -p -- "$(dirname -- "$state_file")" || exit 1
exec 9>"$state_file.lock"
flock -n 9 || { echo '[net] A network operation is already running.' >&2; exit 1; }
# Bound every NetworkManager request, including device release/management.
nm() { LC_ALL=C timeout 50s nmcli "$@"; }
interfaces=(); connections=(); managed=(); was_up=(); macs=(); changed=()
load_state() {
    local record i
    record="$("$helper" "$state_helper" show "$state_file")" || return 1
    mapfile -t saved_rows <<< "$record"
    phy="${saved_rows[0]}"
    interfaces=(); connections=(); managed=(); was_up=(); macs=(); changed=()
    for (( i=1; i<${#saved_rows[@]}; i+=5 )); do
        changed+=("${#interfaces[@]}")
        interfaces+=("${saved_rows[i]}"); connections+=("${saved_rows[i+1]}")
        managed+=("${saved_rows[i+2]}"); was_up+=("${saved_rows[i+3]}"); macs+=("${saved_rows[i+4]}")
    done
}
ensure_radio() {
    local state
    state="$(nm radio wifi)" || return 1
    if [[ "$state" != enabled ]]; then
        echo '[net] Enabling Wi-Fi for the console connection.'
        nm radio wifi on || return 1
        [[ "$(nm radio wifi)" == enabled ]] || { echo '[net] Wi-Fi is blocked. Enable the hardware Wi-Fi switch.' >&2; return 1; }
    fi
}
resolve_device() {
    local old="$1" mac="$2" entry
    if [[ -e "$sys_root/class/net/$old/address" && "$(cat "$sys_root/class/net/$old/address")" == "$mac" ]]; then printf '%s' "$old"; return; fi
    for entry in "$sys_root"/class/net/*/address; do
        [[ -f "$entry" && "$(cat "$entry")" == "$mac" ]] || continue
        basename -- "$(dirname -- "$entry")"; return
    done
    return 1
}
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
            state="$(nm -g GENERAL.STATE device show "$device" 2>/dev/null || true)"
            if [[ "$state" =~ ^(30|40|50|60|70|80|90|100)([^0-9]|$) ]]; then break; fi
            sleep 1
        done
        wait=$((deadline - SECONDS)); (( wait > 45 )) && wait=45
        (( wait > 0 )) || return 1
        nm --wait "$wait" connection up uuid "$conn" ifname "$device" || true
        # An activation result alone is insufficient: wait for this exact
        # profile to be connected and have an IPv4 address before probing TCP.
        for (( probe=0; probe<5 && SECONDS<deadline; probe++ )); do
            state="$(nm -g GENERAL.STATE device show "$device" 2>/dev/null || true)"
            current="$(nm -g GENERAL.CON-UUID device show "$device" 2>/dev/null || true)"
            address="$(nm -g IP4.ADDRESS device show "$device" 2>/dev/null || true)"
            if [[ "$state" =~ ^100([^0-9]|$) && "$current" == "$conn" && -n "$address" ]]; then return 0; fi
            sleep 1
        done
        if [[ "$round" == 1 ]]; then echo '[net] Wi-Fi is not ready yet; retrying the saved connection.'; fi
    done
    return 1
}
recover() {
    local failed=0 i device conn probe linked
    ensure_radio || failed=1
    echo '[net] Restoring the selected Wi-Fi radio; Ethernet was kept connected.'
    # The child has exited. Delete its leftover VIF before raising wlan0; some
    # drivers reject raising the original interface while a station VIF exists.
    if [[ -e "$sys_root/class/net/$temporary" ]]; then
        nm device set "$temporary" managed no >/dev/null 2>&1 || true
        "$helper" "$network_helper" delete --phy "$phy" --ifname "$temporary" || failed=1
    fi
    for i in "${changed[@]}"; do
        device=''
        for (( probe=0; probe<10; probe++ )); do
            device="$(resolve_device "${interfaces[i]}" "${macs[i]}")" && break
            sleep 1
        done
        if [[ -z "$device" ]]; then echo '[net] The original Wi-Fi adapter is missing; reconnect it and restore Wi-Fi.' >&2; failed=1; continue; fi
        conn="${connections[i]}"
        if [[ "${was_up[i]}" == 1 || "${managed[i]}" == yes ]]; then
            linked=0
            for (( probe=0; probe<5; probe++ )); do
                if link "$device" up; then linked=1; break; fi
                sleep 1
            done
            if (( ! linked )) && [[ "${managed[i]}" != yes || -z "$conn" || "$conn" == -- ]]; then failed=1; fi
        else link "$device" down || failed=1; fi
        if [[ "${managed[i]}" == yes ]]; then
            nm device set "$device" managed yes || failed=1
            if [[ -n "$conn" && "$conn" != -- ]]; then
                restore_connection "$device" "$conn" || failed=1
            fi
        fi
    done
    if (( failed )); then
        echo '[net] Wi-Fi restoration failed. Reconnect this radio manually before retrying.' >&2
        return 1
    fi
    rm -f -- "$state_file"
    echo 'CIV7_LDN_NETWORK_RESTORED'
}
restore() {
    local status=$?
    trap - EXIT INT TERM
    recover || { (( status != 0 )) || status=1; }
    exit "$status"
}
requested_phy="$phy"
if [[ -f "$state_file" ]]; then
    load_state || exit 1
    recover || exit 1
elif [[ "$mode" == restore ]]; then
    echo '[net] No interrupted Wi-Fi change to restore.'
    echo 'CIV7_LDN_NETWORK_RESTORED'
fi
[[ "$mode" == restore ]] && exit 0
phy="$requested_phy"
[[ "$phy" =~ ^phy[0-9]+$ && -d "$sys_root/class/ieee80211/$phy" ]] || exit 2
if [[ -e "$sys_root/class/net/$temporary" ]]; then
    echo '[net] A previous LDN interface exists without a recovery record. Restore Wi-Fi before starting.' >&2
    exit 1
fi
ensure_radio || exit 1
interfaces=(); connections=(); managed=(); was_up=(); macs=(); changed=(); record=()
for radio in "$sys_root"/class/net/*/phy80211; do
    [[ "$(basename -- "$(readlink -f -- "$radio")")" == "$phy" ]] || continue
    device="$(basename -- "$(dirname -- "$radio")")"
    conn="$(nm -g GENERAL.CON-UUID device show "$device")" || exit 1
    [[ "$conn" == -- ]] && conn=''
    manager="$(nm -g GENERAL.NM-MANAGED device show "$device")" || exit 1
    [[ "$manager" == yes || "$manager" == no ]] || exit 1
    flags="$(cat "$sys_root/class/net/$device/flags")"; up="$((flags & 1))"
    mac="$(cat "$sys_root/class/net/$device/address")"
    interfaces+=("$device"); connections+=("$conn"); managed+=("$manager"); was_up+=("$up"); macs+=("$mac")
    record+=("$device" "$conn" "$manager" "$up" "$mac")
done
(( ${#interfaces[@]} > 0 )) || { echo '[net] No usable Wi-Fi interface found for this radio.' >&2; exit 1; }
"$helper" "$state_helper" save "$state_file" "$phy" "${record[@]}" || exit 1
child=''
interrupt() {
    if [[ -n "$child" ]]; then
        kill -TERM "$child" 2>/dev/null || true
        for (( probe=0; probe<10; probe++ )); do kill -0 "$child" 2>/dev/null || break; sleep 1; done
        kill -KILL "$child" 2>/dev/null || true
        wait "$child" 2>/dev/null || true
    fi
    exit "$1"
}
trap restore EXIT
trap 'interrupt 130' INT
trap 'interrupt 143' TERM
for i in "${!interfaces[@]}"; do
    changed+=("$i")
    device="${interfaces[i]}"
    if [[ "${managed[i]}" == yes ]]; then nm device set "$device" managed no || exit 1; fi
    link "$device" down || exit 1
done
echo '[net] Selected Wi-Fi radio released for LDN. The temporary interface handles the console connection; Ethernet remains connected.'
"$@" <&0 &
child=$!
wait "$child"
exit $?
