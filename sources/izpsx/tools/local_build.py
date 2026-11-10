"""Local tool selection. No downloads, installations or console access."""
from pathlib import Path
import os,shutil
ROOT=Path(__file__).resolve().parents[1]
PANEL=Path(os.environ.get('IZPSX_PANEL',os.environ.get('IZ2S_ROOT',str(ROOT.parent/'IZ2S-Platform')))).resolve()
CC=Path(os.environ.get('IZPSX_CC',shutil.which('aarch64-linux-musl-gcc') or 'aarch64-linux-musl-gcc'))
PREFIX=str(CC)[:-3]
QEMU=Path(os.environ.get('IZPSX_QEMU',shutil.which('qemu-aarch64') or 'qemu-aarch64'))
def mirror_link(destination,source):
    if destination.is_symlink():
        if os.readlink(destination)==str(source):return
        destination.unlink()
    if not destination.exists():destination.symlink_to(source)
