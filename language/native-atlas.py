"""Compatibility entry for already-English packaged console atlases."""
import sys

kind, shell, language = sys.argv[1:]
if kind not in ('focus', 'legacy') or shell not in ('izlauncher', 'izpslauncher'):
    raise ValueError('Invalid interface atlas')
if language != 'en':
    raise ValueError('Console interfaces use English')
sys.stdout.buffer.write(sys.stdin.buffer.read())
