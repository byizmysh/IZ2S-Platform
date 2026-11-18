"""Repack the launcher using the console's unchanged game policy."""
import struct


def parse(payload):
    if len(payload) < 16 or payload[:4] != b'PFS0':
        raise ValueError('Invalid launcher PFS0 header')
    count, strings, reserved = struct.unpack_from('<III', payload, 4)
    start = 16 + count * 24 + strings
    if count != 2 or reserved or not strings or start > len(payload):
        raise ValueError('Invalid launcher PFS0 directory')
    names = payload[16 + count * 24:start]
    files, ranges = {}, []
    for i in range(count):
        offset, size, name, reserved = struct.unpack_from('<QQII', payload, 16 + 24 * i)
        if reserved or name >= len(names) or b'\0' not in names[name:]:
            raise ValueError('Invalid launcher PFS0 member')
        label = names[name:names.index(0, name)].decode('ascii')
        if label not in ('main', 'main.npdm') or label in files or size > len(payload) - start or offset > len(payload) - start - size:
            raise ValueError('Invalid launcher PFS0 file range')
        if any(offset < end and begin < offset + size for begin, end in ranges):
            raise ValueError('Overlapping launcher PFS0 members')
        ranges.append((offset, offset + size))
        files[label] = payload[start + offset:start + offset + size]
    if not files['main'].startswith(b'NSO0') or not files['main.npdm'].startswith(b'META'):
        raise ValueError('Invalid launcher image or game policy')
    return files


def build(image, policy):
    names = b'main\0main.npdm\0'
    start = 16 + 2 * 24 + len(names)
    policy_at = (len(image) + 31) & ~31
    result = bytearray(start + policy_at + len(policy))
    struct.pack_into('<4sIII', result, 0, b'PFS0', 2, len(names), 0)
    struct.pack_into('<QQII', result, 16, 0, len(image), 0, 0)
    struct.pack_into('<QQII', result, 40, policy_at, len(policy), 5, 0)
    result[64:start] = names
    result[start:start + len(image)] = image
    result[start + policy_at:] = policy
    assert parse(result) == {'main': image, 'main.npdm': policy}
    return bytes(result)
