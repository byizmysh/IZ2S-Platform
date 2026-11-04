"""Shared text selection for native IZ2S rasterizers."""
from pathlib import Path
import json, os
ROOT = Path(__file__).resolve().parent.parent
try:
    language = json.loads((ROOT / 'data/state/language.json').read_text()).get('language', 'en')
except FileNotFoundError:
    language = 'en'
if language not in ('en', 'ru', 'es', 'zh'): language = 'en'
messages = json.loads((ROOT / 'language' / language / 'ui.json').read_text())
def t(text):
    return messages.get(str(text), str(text))
def font():
    choices = ([os.environ.get('HB_CJK_FONT',''), '/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc'] if language == 'zh' else []) + [os.environ.get('HB_FONT',''), str(ROOT / 'language/fonts/DejaVuSans.ttf'), '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', '/usr/share/fonts/TTF/DejaVuSans.ttf']
    return next(item for item in choices if item and Path(item).is_file())
