#!/usr/bin/env python3
"""Windows entry point for the same local health dashboard."""
from pathlib import Path
import runpy

ROOT = Path(__file__).resolve().parents[1]
runpy.run_path(str(ROOT / 'mac' / 'check_health.py'), run_name='__main__')
