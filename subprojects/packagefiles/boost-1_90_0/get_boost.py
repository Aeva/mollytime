import os
import subprocess
import sys

from argparse import ArgumentParser
from pathlib import Path

parser = ArgumentParser()
_ = parser.add_argument("modules", nargs = "*", help = "List of Boost submodules to acquire.")

args = parser.parse_args()
boost_modules: list[str] = args.modules # pyright: ignore[reportAny]

# Boost tools need the boost directory to be current working directory.
boost_dir = Path(__file__).parent
os.chdir(boost_dir)

# We're going to first download each required submodule...
get_submodule_args = [ "git", "submodule", "update", "--depth", "1", "-q", "--init" ]

# ...Then use Boost's dependency manager to automatically download their dependencies.
# (See: https://www.boost.org/doc/user-guide/getting-started.html#_individual_modules)
boost_depinst_path = Path("tools/boostdep/depinst/depinst.py")

# Acquire the `boostdep` module;
get_boostdep_command = get_submodule_args + [ "tools/boostdep" ]
get_boostdep_process = subprocess.run(get_boostdep_command)
get_boostdep_process.check_returncode()

for module in boost_modules:
    # Acquire this module.
    get_module_command = get_submodule_args + [ f"libs/{module}" ]
    get_module_process = subprocess.run(get_module_command)
    get_module_process.check_returncode()

    # Install its dependencies.
    # (Yes, that's correct, "--depth 1" is a single argument. `depinst.py` appends this verbatim to a git command line.)
    dep_install_command = [ sys.executable, boost_depinst_path, "-X", "test", "-g", "--depth 1", module ]
    dep_install_process = subprocess.run(dep_install_command)
    dep_install_process.check_returncode()