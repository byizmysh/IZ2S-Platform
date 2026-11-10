"""English text selection for console rasterizers; panel locales are separate."""
from pathlib import Path
import json, os
ROOT = Path(__file__).resolve().parent.parent
language = 'en'
messages = json.loads((ROOT / 'language/en/ui.json').read_text())
def t(text):
    return messages.get(str(text), str(text))
def font():
    choices = [os.environ.get('HB_FONT',''), str(ROOT / 'language/fonts/DejaVuSans.ttf'), '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', '/usr/share/fonts/TTF/DejaVuSans.ttf']
    return next(item for item in choices if item and Path(item).is_file())
