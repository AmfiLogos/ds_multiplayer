#!/usr/bin/env python3
"""L3 lane shortcut: win/wloader.py (the Windows copy through the mod loader)."""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "win"))
from wloader import *          # noqa
if __name__ == "__main__":
    import runpy; runpy.run_path(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "win", "wloader.py"), run_name="__main__")
