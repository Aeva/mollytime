import os
import shutil
import subprocess
import sys

from pathlib import Path

this_dir = Path(__file__).parent
project_dir = this_dir.parent

def _get_boost(desired_boost_modules: list[str]):
    get_submodule_args = [ "git", "submodule", "update", "--depth", "1", "-q", "--init" ]
    boost_depinst_path = Path("tools/boostdep/depinst/depinst.py")

    # Boost tools need the boost directory to be current working directory.
    boost_dir = project_dir / "third_party" / "boost_1_90_0"
    old_cwd = os.getcwd()

    try:
        os.chdir(boost_dir)

        # Acquire the boost dependency manager.
        get_boostdep_process = subprocess.run(get_submodule_args + [ "tools/boostdep" ])
        get_boostdep_process.check_returncode()

        for module in desired_boost_modules:
            # Acquire this module.
            get_module_process = subprocess.run(get_submodule_args + [ f"libs/{module}" ])
            get_module_process.check_returncode()

            # Install its dependencies.
            # (Yes, that's correct, "--depth 1" is a single argument. `depinst.py` appends this verbatim to a git command line.)
            dep_install_process = subprocess.run([ sys.executable, boost_depinst_path, "-X", "test", "-g", "--depth 1", module ])
            dep_install_process.check_returncode()
    
    finally:
        os.chdir(old_cwd)

def _get_sdl3_ttf_dependencies():
    sdl_ttf_external = project_dir / "third_party" / "SDL3_ttf-3.2.2" / "external"
    if shutil.which("/bin/bash"):
        sh_result = subprocess.run([ sdl_ttf_external / "downloads.sh" ], executable = "/bin/bash")
        sh_result.check_returncode()
    elif shutil.which("powershell"):
        psh_result = subprocess.run([ "powershell", "-Command", f"& '{sdl_ttf_external / "Get-GitModules.ps1"}'" ])
        psh_result.check_returncode()
    else:
        raise FileNotFoundError("Can't find Bash or Powershell, so I can't resolve SDL_ttf dependencies.")

# This is all git stuff.
if shutil.which("git") == None:
    raise FileNotFoundError("Can't find 'git' on PATH. I need Git to get submodules.")

print("Getting submodules...", flush = True)

# Switch to the project root.
this_dir = Path(__file__).parent
project_dir = this_dir.parent
os.chdir(project_dir)

# Init and update base submodules.
init_process = subprocess.run([ "git", "submodule", "init" ])
init_process.check_returncode()

update_process = subprocess.run([ "git", "submodule", "update" ])
update_process.check_returncode()

# Gather dependencies' dependencies.
_get_boost([ "atomic", "stacktrace" ])
_get_sdl3_ttf_dependencies()

print("...done getting submodules.", flush = True)