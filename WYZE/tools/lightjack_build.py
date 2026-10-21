"""Lightjack laboratory target, 2026-10-21. GPL-3.0-only."""
from SCons.Script import COMMAND_LINE_TARGETS

# The stock upload recipe writes a bootloader and partition table as well as the
# application. This target is app-only through an already validated Wyze loader.
if any("upload" in target.lower() or "erase" in target.lower()
       for target in COMMAND_LINE_TARGETS):
    raise RuntimeError("Lightjack: direct upload/erase is disabled. Use the verified Wyze loader with an app-only image after a complete backup.")
