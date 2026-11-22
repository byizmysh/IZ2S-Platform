"""Compatibility command for the IZ2s font generator."""
import runpy
from pathlib import Path
runpy.run_path(str(Path(__file__).resolve().parents[2]/'shells/izlauncher/server/launcher-text.py'),run_name='__main__')
