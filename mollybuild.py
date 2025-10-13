import os
import platform
import subprocess
import sys
import sysconfig

from argparse import ArgumentParser, Namespace
from configparser import ConfigParser
from pathlib import Path

def _get_dependencies(dependencies: list[str]):
    pip_args = [ sys.executable, "-m", "pip", "install" ] + dependencies
    pip_result = subprocess.run(pip_args)
    return pip_result.returncode

# HACK: UGH: So, meson-python "helpfully" overrides some built-in Meson options:
# - `buildtype  = release`
# - `b_ndebug   = if-release`
# - `b_vscrt    = md`
# 
# See: https://mesonbuild.com/meson-python/explanations/default-options.html
# 
# These overrides are passed in via the CLI. While meson-python ensures that our `setup-args`
# are passed in next, and thus theoretically given higher priority, that doesn't actually work
# in all cases: we define our build options using native files, which are always lower-priority
# than CLI args, regardless of their CLI position. That means our native file options are ignored.
# We have no recourse to prevent this. The meson-python CLI args are hardcoded.
# See: `mesonpy/__init__.py`, `_configure()`
# 
# To work around this, see if our native files have any of the overridden values, and extract
# them to pass in as CLI overrides. The options we're interested in are close enough to .ini
# format that ConfigParser will suffice; we just need to strip the '' quotes from values.
def _HACK_extract_override_overrides(native_file: Path) -> list[str]:
    args: list[str] = []
    mode_config = ConfigParser()
    mode_config.read(native_file)

    def _parse_arg(name: str):
        arg = mode_config.get("built-in options", name, fallback = None)
        if arg != None:
            arg = arg.strip("'")
            args.append(f"-Csetup-args=-D{name}={arg}")

    _parse_arg("buildtype")
    _parse_arg("b_ndebug")
    _parse_arg("b_vscrt")
    
    return args

def setup(modes: dict[str, Path], toolchains: dict[str, Path], args: Namespace):
    # Grab dependencies.
    pip_result = _get_dependencies([ "meson", "meson-python", "ninja", "pybind11", "pyinstaller", "pygame" ])
    if pip_result != 0:
        return pip_result

    # Prepare to execute a local, editable `pip install`.
    # This will have meson-python automatically run `meson setup`, and then add a launcher shim
    # that automatically recompiles our extension module(s) when running the module locally.
    args_dict = vars(args)
    install_args = [ sys.executable, "-m", "pip", "install", "--no-build-isolation" ]

    # This isn't that "verbose," it's just the normal `meson compile` output.
    # We really need this, because it includes compile errors!
    install_args += [ "-Ceditable-verbose=true" ]
    
    # Gather mode config, if specified.
    mode_name = args_dict.get("mode", None)
    if mode_name != None:
        mode_file = modes.get(mode_name, None)
        if mode_file != None:
            mode_arg = f"--native-file={mode_file.resolve()}"
            install_args += [ f"-Csetup-args={mode_arg}" ]
            install_args += _HACK_extract_override_overrides(mode_file)
    
    # Gather toolchain config, if specified.
    toolchain_name = args_dict.get("toolchain", None)
    if toolchain_name != None:
        toolchain_file = toolchains.get(toolchain_name, None)
        if toolchain_file != None:
            toolchain_arg = f"--native-file={toolchain_file.resolve()}"
            install_args += [ f"-Csetup-args={toolchain_arg}" ]
            install_args += _HACK_extract_override_overrides(toolchain_file)

    # Go.
    install_args += [ "-v", "--editable", "." ]
    install_result = subprocess.run(install_args)
    return install_result.returncode

def exe(modes: dict[str, Path], toolchains: dict[str, Path], args: Namespace):
    # Get the build directory. Meson-python will set this to './build/cpXX`,
    # where XX is the Python major & minor version number, w/o decimal separators.
    major, minor, _ = platform.python_version().split(".")
    build_dir_local = Path("build") / f"cp{major}{minor}"

    # If this doesn't exist, the user probably forgot to run `setup`.
    this_dir = Path(__file__).parent
    build_dir = this_dir / build_dir_local
    if not build_dir.exists():
        print(f"Error: I can't find the expected build directory: '{build_dir_local}' (i.e. '{build_dir}').\nDid you forget to run `setup`?")
        return 1
    
    # `meson compile` the pyinstaller target.
    compile_args = [ "meson", "compile", "-C", str(build_dir.resolve()), "mollytime-exe" ]
    compile_result = subprocess.run(compile_args)
    if compile_result.returncode != 0:
        return compile_result.returncode

    # Now, `meson install` it.
    install_args = [ "meson", "install", "--no-rebuild", "--tags=exe", "-C", str(build_dir.resolve()) ]
    install_result = subprocess.run(install_args)
    return install_result.returncode

def package(modes: dict[str, Path], toolchains: dict[str, Path], args: Namespace):
    # Grab dependencies.
    pip_result = _get_dependencies([ "build" ])
    if pip_result != 0:
        return pip_result

    # Prepare to execute `py(thon) -m build`.
    args_dict = vars(args)
    setup_args = [ sys.executable, "-m", "build" ]

    # Always package in `release` mode.
    mode_file = modes["release"]
    mode_arg = f"--native-file={mode_file.resolve()}"
    setup_args += [ f"-Csetup-args={mode_arg}" ]
    
    # Gather toolchain config, if specified.
    toolchain_name = args_dict.get("toolchain", None)
    if toolchain_name != None:
        toolchain_file = toolchains.get(toolchain_name, None)
        if toolchain_file != None:
            toolchain_arg = f"--native-file={toolchain_file.resolve()}"
            setup_args += [ f"-Csetup-args={toolchain_arg}" ]
    
    # Go.
    package_result = subprocess.run(setup_args)
    return package_result.returncode

if __name__ == "__main__":
    working_dir = Path(__file__).parent
    os.chdir(working_dir)

    # Parse config files, and map them to their identifying name.
    config_dir = Path("build_native")
    config_files = [ file for file in config_dir.iterdir() if file.suffix == ".ini" ]
    modes = {
        file.stem.removeprefix("mode-") : file
        for file in config_files
        if file.stem.startswith("mode-")
    }
    toolchains = {
        file.stem.removeprefix("toolchain-") : file
        for file in config_files
        if file.stem.startswith("toolchain-")
    }

    # ---

    # Main parser
    parser = ArgumentParser(
        prog = "mollybuild",
        description = \
            "Concise build helper."
            "\nFor an iterative 'development' workflow, run `setup`, then just run the project with `python -m mollytime`. C++ changes will be automatically recompiled when you run."
            "\nWhen you're ready to distribute, run `package`.",
        argument_default = "-h"
    )
    subparsers = parser.add_subparsers(title = "Commands")

    # `setup` command
    setup_parser = subparsers.add_parser("setup", help = "Development: Set up a development build environment. Once complete, you can just run the project with `python -m mollytime`. C++ changes will be automatically recompiled when you run.")
    setup_parser.set_defaults(command = setup)
    setup_parser.add_argument(
        "mode",
        help = "Build mode. If unspecified, uses `debug`.",
        choices = modes.keys()
    )
    setup_parser.add_argument(
        "toolchain",
        help = "Toolchain to build with. If unspecified, uses your system default, which might not be in this list.",
        choices = toolchains.keys()
    )

    # `package` command
    package_parser = subparsers.add_parser("package", help = "Release: Build a distributable Python package (sdist and wheel).")
    package_parser.set_defaults(command = package)
    package_parser.add_argument(
        "toolchain",
        help = "Toolchain to build with. If unspecified, uses your system default.",
        choices = toolchains.keys()
    )

    # `exe` command
    exe_parser = subparsers.add_parser("exe", help = "Release: Build an executable with Pyinstaller. You'll need to run `setup` first.")
    exe_parser.set_defaults(command = exe)

    # Go
    args = parser.parse_args()
    if len(vars(args)) == 0:
        parser.print_help()
        exit(0)
    
    return_code = args.command(modes, toolchains, args)
    exit(return_code)