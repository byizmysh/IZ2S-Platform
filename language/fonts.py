"""Resolve bundled fonts, with an optional user-provided override."""
import os
from pathlib import Path


def font(name='DejaVuSans.ttf'):
    explicit = os.environ.get('HB_FONT')
    folders = ([Path(explicit).parent] if explicit else []) + [
        Path(__file__).resolve().with_name('fonts'),
        Path('/usr/share/fonts/truetype/dejavu'), Path('/usr/share/fonts/TTF')]
    for folder in folders:
        candidate = folder / name
        if candidate.is_file():
            return str(candidate)
    if explicit and Path(explicit).is_file():
        return explicit
    if name != 'DejaVuSans.ttf':
        return font()
    raise FileNotFoundError('Install DejaVu Sans (fonts-dejavu-core or ttf-dejavu)')
