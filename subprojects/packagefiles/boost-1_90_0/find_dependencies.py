import os

from argparse import ArgumentParser
from pathlib import Path

# We're going to patch into `depinst.py`, so we can hijack its module-scanner.
from tools.boostdep.depinst.depinst import read_exceptions, read_gitmodules, scan_module_dependencies  # pyright: ignore[reportUnknownVariableType]

parser = ArgumentParser()
_ = parser.add_argument("modules", nargs = "+", help = "Modules to scan for dependencies.")

args = parser.parse_args()
modules: list[str] = args.modules  # pyright: ignore[reportAny]

# `depinst.py` hard-assumes that it's being invoked directly (via reading `sys.argv[0]`).
# We need to trick its subsequent path calculations into being correct by manipulating the CWD.
boost_dir = Path(__file__).parent
depinst_dir = boost_dir / 'tools' / 'boostdep' / 'depinst'

# Read exceptions relative to `depinst.py`.
os.chdir(depinst_dir)
exceptions: dict[str, str] = read_exceptions()  # pyright: ignore[reportUnknownVariableType]

# Read gitmodules relative to the Boost tree.
os.chdir(boost_dir)
git_modules: list[str] = read_gitmodules()  # pyright: ignore[reportUnknownVariableType]

# OK, we're ready to scan for dependencies.
# Refer to `depinst.py` for these values, just in case they change in the future.
modules_always_required = [ 'config', 'headers' ]   # `./tools` modules are omitted, as they have no installed library representatioon.
modules_always_required.append('static_assert')     # For whatever reason, this oft-required dependency isn't picked up by `depinst.py`.
scan_dirs = [ 'include', 'src' ]
dependencies: dict[str, int] = {}

for module in modules_always_required + modules:
    dependencies[module] = 1
    scan_module_dependencies(module, exceptions, git_modules, dependencies, scan_dirs, [])

# We don't care if they're installed or not. Just print them out as a line-separated list.
for module in dependencies.keys():
    print(module)