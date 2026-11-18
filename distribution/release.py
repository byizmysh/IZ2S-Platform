"""Repackage verified installer trees; see BUILDING.md for required inputs."""
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).resolve().parents[1] / 'tools/release/package-prepared.py'), run_name='__main__')
