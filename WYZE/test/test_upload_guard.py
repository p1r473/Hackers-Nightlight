"""Exercise the build guard without loading PlatformIO or touching hardware."""
import runpy
from pathlib import Path
import sys
import types

script = Path(__file__).resolve().parents[1] / "tools/lightjack_build.py"
parent = types.ModuleType("SCons")
module = types.ModuleType("SCons.Script")
sys.modules["SCons"] = parent
sys.modules["SCons.Script"] = module
for targets in ([], ["build"], ["clean"]):
    module.COMMAND_LINE_TARGETS = targets
    runpy.run_path(str(script))
for target in ("upload", "uploadfs", "uploadfsota", "erase"):
    module.COMMAND_LINE_TARGETS = [target]
    try:
        runpy.run_path(str(script))
    except RuntimeError:
        pass
    else:
        raise AssertionError(target)
print("PASS: 7 upload-guard simulations, no hardware commands invoked")
