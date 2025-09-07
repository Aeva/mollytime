import os
import subprocess
import sys

from argparse import ArgumentParser, Namespace
from pathlib import Path

def _get_dependencies(dependencies: list[str]):
    pip_args = [ sys.executable, "-m", "pip", "install" ] + dependencies
    pip_result = subprocess.run(pip_args)
    return pip_result.returncode

def setup(modes: dict[str, Path], toolchains: dict[str, Path], args: Namespace):
    # Grab dependencies.
    pip_result = _get_dependencies([ "meson", "ninja", "pybind11", "pygame" ])
    if pip_result != 0:
        return pip_result

    # Prepare to execute `meson setup`.
    args_dict = vars(args)
    setup_args = [ "meson", "setup", "-Dworkflow=development" ]
    
    # Gather mode config, if specified.
    mode_name = args_dict.get("mode", None)
    if mode_name != None:
        mode_file = modes.get(mode_name, None)
        if mode_file != None:
            setup_args += [ "--native-file", str(mode_file.resolve()) ]
    
    # Gather toolchain config, if specified.
    toolchain_name = args_dict.get("toolchain", None)
    if toolchain_name != None:
        toolchain_file = toolchains.get(toolchain_name, None)
        if toolchain_file != None:
            setup_args += [ "--native-file", str(toolchain_file.resolve()) ]
    
    # Go.
    setup_args += [ "build" ]
    setup_result = subprocess.run(setup_args)
    return setup_result.returncode

def build(modes: dict[str, Path], toolchains: dict[str, Path], args: Namespace):
    # Compile.
    compile_result = subprocess.run([ "meson", "compile", "-C", "build" ])
    if compile_result.returncode != 0:
        return compile_result.returncode

    # If successful, install.
    install_result = subprocess.run([ "meson", "install", "--no-rebuild", "-C", "build" ])
    return install_result.returncode

def package(modes: dict[str, Path], toolchains: dict[str, Path], args: Namespace):
    # Grab dependencies.
    pip_result = _get_dependencies([ "build" ])
    if pip_result != 0:
        return pip_result

    # Prepare to execute `py(thon) -m build`.
    args_dict = vars(args)
    workflow_arg = "-Dworkflow=packaging"
    setup_args = [ sys.executable, "-m", "build", f"-Csetup-args={workflow_arg}" ]

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
            "\nFor an iterative 'development' workflow, run `setup`, then `build`."
            "\nWhen you're ready to distribute, run `package`.",
        argument_default = "-h"
    )
    subparsers = parser.add_subparsers(title = "Commands")

    # `setup` command
    setup_parser = subparsers.add_parser("setup", help = "Development: Set up a development build environment.")
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

    # `build` command
    build_parser = subparsers.add_parser("build", help = "Development: Execute the build environment created by `setup`.")
    build_parser.set_defaults(command = build)

    # `package` command
    package_parser = subparsers.add_parser("package", help = "Release: Build a distributable Python package (sdist and wheel).")
    package_parser.set_defaults(command = package)
    package_parser.add_argument(
        "toolchain",
        help = "Toolchain to build with. If unspecified, uses your system default.",
        choices = toolchains.keys()
    )

    # Go
    args = parser.parse_args()
    if len(vars(args)) == 0:
        parser.print_help()
        exit(0)
    
    return_code = args.command(modes, toolchains, args)
    exit(return_code)