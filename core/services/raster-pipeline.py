"""Render a catalog and its shell additions in one local Python process."""
from pathlib import Path
import runpy
import sys
import os

source, output, *steps = sys.argv[1:]
scripts = [str(Path(__file__).with_name('catalog-assets.py')), *steps]
for index, script in enumerate(scripts):
    # Match a direct script invocation, including sibling helpers such as
    # ps_fonts. Each step keeps its original __file__ and isolated globals.
    previous_path = list(sys.path)
    previous_argv = sys.argv
    previous_font = os.environ.get('HB_FONT')
    try:
        if index == 0 and os.environ.get('IZ2S_RASTER_FONT'):
            os.environ['HB_FONT'] = os.environ['IZ2S_RASTER_FONT']
        sys.path.insert(0, str(Path(script).resolve().parent))
        sys.argv = [script, source, output]
        runpy.run_path(script, run_name='__main__')
    finally:
        sys.path[:] = previous_path
        sys.argv = previous_argv
        if previous_font is None:
            os.environ.pop('HB_FONT', None)
        else:
            os.environ['HB_FONT'] = previous_font
