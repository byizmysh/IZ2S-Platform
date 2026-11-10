"""Verify the JS loader protocol without installing or changing game saves."""
import secrets
import socket
import sys
from send_js import send

marker = 'IZ2S_CIV_' + secrets.token_hex(12)
port = int(sys.argv[2]) if len(sys.argv) > 2 else 9026
try:
    reply = send(sys.argv[1], port, "(async()=>{await log('" + marker + "')})()", timeout=8)
except ConnectionRefusedError:
    raise SystemExit('CIVSWITCH_LOADER_NOT_RUNNING')
except (TimeoutError, socket.timeout):
    raise SystemExit('CIVSWITCH_LOADER_TIMEOUT')
except OSError:
    raise SystemExit('CIVSWITCH_LOADER_NETWORK_ERROR')
if not reply:
    raise SystemExit('CIVSWITCH_LOADER_TIMEOUT')
if marker not in reply or 'Executed' not in reply:
    raise SystemExit('Remote JS Loader protocol was not confirmed')
print('CIVSWITCH_LOADER_VERIFIED')
