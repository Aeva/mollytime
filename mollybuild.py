import os
import platform
import shutil
import subprocess
import sys

from argparse import ArgumentParser, Namespace
from configparser import ConfigParser
from errno import ENOENT
from pathlib import Path

def _get_dependencies(dependencies: list[str]):
    pip_args = [ sys.executable, "-m", "pip", "install" ] + dependencies
    pip_result = subprocess.run(pip_args)
    pip_result.check_returncode()

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
    _ = mode_config.read(native_file)

    def _parse_arg(name: str):
        arg = mode_config.get("built-in options", name, fallback = None)
        if arg != None:
            arg = arg.strip("'")
            args.append(f"-Csetup-args=-D{name}={arg}")

    _parse_arg("buildtype")
    _parse_arg("b_ndebug")
    _parse_arg("b_vscrt")
    
    return args

def clean(_modes: dict[str, Path], _toolchains: dict[str, Path], _args: Namespace):
    build_dir = Path("build")
    dist_dir = Path("dist")
    third_party_dir = Path("third_party")

    shutil.rmtree(build_dir, True)
    shutil.rmtree(dist_dir, True)

    for submodule_dir in third_party_dir.iterdir():
        # HACK
        if submodule_dir.name == "VAStateVariableFilter":
            continue
        
        for everything in submodule_dir.iterdir():
            if everything.is_dir():
                shutil.rmtree(everything, True)
            else:
                os.remove(everything)

def init(_modes: dict[str, Path], toolchains: dict[str, Path], _args: Namespace):
    args_dict = vars(args)
    project_dir = Path(__file__).parent
    build_tools_dir = project_dir / "build_tools"

    # Dig out the specified compiler from the provided toolchain file.
    # The build  helpers will pass these to CMake.
    c_compiler_args: list[str] = []
    cpp_compiler_args: list[str] = []
    linker_type_args: list[str] = []

    toolchain_name: str | None = args_dict.get("toolchain", None)
    if toolchain_name != None:
        toolchain_file = toolchains.get(toolchain_name, None)
        if toolchain_file != None:
            toolchain_config = ConfigParser()
            _ = toolchain_config.read(toolchain_file)
            
            c_compiler: str | None = toolchain_config.get("binaries", "c", fallback = None)
            if c_compiler != None:
                c_compiler_args = [ "--c-compiler", c_compiler ]
            
            cpp_compiler: str | None = toolchain_config.get("binaries", "cpp", fallback = None)
            if cpp_compiler != None:
                cpp_compiler_args = [ "--cpp-compiler", cpp_compiler ]
            
            linker_type: str | None = toolchain_config.get("binaries", "cpp_ld", fallback = None)
            if linker_type != None:
                match linker_type:
                    case "link":
                        linker_type = "MSVC"
                    case "lld":
                        linker_type = "LLD"
                    case _:
                        linker_type = "SYSTEM"
                
                linker_type_args = [ "--linker-type", linker_type ]

    # Grab dependencies.
    _get_dependencies([ "cmake", "meson", "meson-python", "ninja", "pyinstaller" ])
    
    # Get submodules. This will acquire *only* the Boost submodules we require.
    get_submodules_script = build_tools_dir / "get_submodules.py"
    get_submodules_result = subprocess.run([ sys.executable, get_submodules_script ])
    get_submodules_result.check_returncode()
    
    # Build Boost. (Yeah, we're using header-only librarires, but this still has to generate them.)
    boost_build_script = build_tools_dir / "boost_build.py" 
    boost_build_dir = project_dir / "build" / "boost"
    boost_build_result = subprocess.run([ sys.executable, boost_build_script, boost_build_dir ] + cpp_compiler_args + linker_type_args)
    boost_build_result.check_returncode()
    
    # Build SDL3.
    sdl3_build_script = build_tools_dir / "sdl3_build.py" 
    sdl3_build_dir = project_dir / "build" / "sdl3"
    sdl3_build_result = subprocess.run([ sys.executable, sdl3_build_script, sdl3_build_dir ] + c_compiler_args + cpp_compiler_args + linker_type_args)
    sdl3_build_result.check_returncode()
    
    # Build SDL3_ttf.
    sdl3_ttf_build_script = build_tools_dir / "sdl3_ttf_build.py" 
    sdl3_ttf_build_dir = project_dir / "build" / "sdl3_ttf"
    sdl3_ttf_build_result = subprocess.run([ sys.executable, sdl3_ttf_build_script, sdl3_ttf_build_dir, sdl3_build_dir ] + c_compiler_args + cpp_compiler_args + linker_type_args)
    sdl3_ttf_build_result.check_returncode()

def build(modes: dict[str, Path], toolchains: dict[str, Path], args: Namespace):
    # Prepare to execute a local, editable `pip install`.
    # This will have meson-python automatically run `meson setup`, and then add a launcher shim
    # that automatically recompiles our extension module(s) when running the module locally.
    args_dict = vars(args)
    install_args = [ sys.executable, "-m", "pip", "install", "--no-build-isolation" ]

    # This isn't that "verbose," it's just the normal `meson compile` output.
    # We really need this, because it includes compile errors!
    install_args += [ "-Ceditable-verbose=true" ]
    
    # Gather mode config, if specified.
    mode_name: str | None = args_dict.get("mode", None)
    if mode_name != None:
        mode_file = modes.get(mode_name, None)
        if mode_file != None:
            mode_arg = f"--native-file={mode_file.resolve()}"
            install_args += [ f"-Csetup-args={mode_arg}" ]
            install_args += _HACK_extract_override_overrides(mode_file)
    
    # Gather toolchain config, if specified.
    toolchain_name: str | None = args_dict.get("toolchain", None)
    if toolchain_name != None:
        toolchain_file = toolchains.get(toolchain_name, None)
        if toolchain_file != None:
            toolchain_arg = f"--native-file={toolchain_file.resolve()}"
            install_args += [ f"-Csetup-args={toolchain_arg}" ]
            install_args += _HACK_extract_override_overrides(toolchain_file)

    # Go.
    install_args += [ "-v", "--editable", "." ]
    install_result = subprocess.run(install_args)
    install_result.check_returncode()

def exe(_modes: dict[str, Path], _toolchains: dict[str, Path], _args: Namespace):
    # Get the build directory. Meson-python will set this to './build/cpXX`,
    # where XX is the Python major & minor version number, w/o decimal separators.
    major, minor, _ = platform.python_version().split(".")
    build_dir_local = Path("build") / f"cp{major}{minor}"

    # If this doesn't exist, the user probably forgot to run `setup`.
    this_dir = Path(__file__).parent
    build_dir = this_dir / build_dir_local
    if not build_dir.exists():
        raise FileNotFoundError(ENOENT, os.strerror(ENOENT), f"I can't find the expected build directory: '{build_dir_local}' (i.e. '{build_dir}').\nDid you forget to run `setup`?")
    
    # `meson compile` the pyinstaller target.
    compile_args = [ "meson", "compile", "-C", str(build_dir.resolve()), "mollytime-exe" ]
    compile_result = subprocess.run(compile_args)
    compile_result.check_returncode()

    # Now, `meson install` it.
    install_args = [ "meson", "install", "--no-rebuild", "--tags=exe", "-C", str(build_dir.resolve()) ]
    install_result = subprocess.run(install_args)
    install_result.check_returncode()

def package(modes: dict[str, Path], toolchains: dict[str, Path], args: Namespace):
    # Grab dependencies.
    _get_dependencies([ "build" ])

    # Prepare to execute `py(thon) -m build`.
    args_dict = vars(args)
    setup_args = [ sys.executable, "-m", "build", "--no-isolation" ]

    # Always package in `release` mode.
    mode_file = modes["release"]
    mode_arg = f"--native-file={mode_file.resolve()}"
    setup_args += [ f"-Csetup-args={mode_arg}" ]
    
    # Gather toolchain config, if specified.
    toolchain_name: str | None = args_dict.get("toolchain", None)
    if toolchain_name != None:
        toolchain_file = toolchains.get(toolchain_name, None)
        if toolchain_file != None:
            toolchain_arg = f"--native-file={toolchain_file.resolve()}"
            setup_args += [ f"-Csetup-args={toolchain_arg}" ]
    
    # Go.
    package_result = subprocess.run(setup_args)
    package_result.check_returncode()

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
            "Concise build helper." +
            "\nRun `init` after cloning the project. Then, for an iterative 'development' workflow, first run `build`, then just run the project with `python -m mollytime`. C++ changes will be automatically recompiled when you run." +
            "\nWhen you're ready to distribute, run `package` to build a wheel, or `exe` to build a Pyinstaller distribution.",
        argument_default = "-h"
    )
    subparsers = parser.add_subparsers(title = "Commands")

    # `clean` command
    clean_parser = subparsers.add_parser("clean")
    clean_parser.set_defaults(command = clean)
    
    # `init` command
    init_parser = subparsers.add_parser("init", help = \
        "Initialize the project repository. This will:" +
        "\n- Acquire dependent Python packages via pip. (Running in a virtual environment is highly recommended!)" +
        "\n- Initialize and update third party Git submodules. (Git is required!)" +
        "\n- Build third party dependencies." +
        "\n" +
        "\nWhile Mollytime uses Meson, third-party dependences will, regrettably, be built using CMake. I'll handle it all; just FYI."
    )
    init_parser.set_defaults(command = init)
    _ = init_parser.add_argument(
        "toolchain",
        help = "Toolchain to build dependencies with. If unspecified, uses your system default.",
        choices = toolchains.keys()
    )

    # `build` command
    build_parser = subparsers.add_parser("build", help = "Development: Build a working environment. Once complete, you can just run the project with `python -m mollytime`. C++ changes will be automatically recompiled when you run.")
    build_parser.set_defaults(command = build)
    _ = build_parser.add_argument(
        "mode",
        help = "Build mode. If unspecified, uses `debug`.",
        choices = modes.keys()
    )
    _ = build_parser.add_argument(
        "toolchain",
        help = "Toolchain to build with. If unspecified, uses your system default, which might not be in this list.",
        choices = toolchains.keys()
    )

    # `package` command
    package_parser = subparsers.add_parser("package", help = "Release: Build a distributable Python package (sdist and wheel).")
    package_parser.set_defaults(command = package)
    _ = package_parser.add_argument(
        "toolchain",
        help = "Toolchain to build with. If unspecified, uses your system default.",
        choices = toolchains.keys()
    )

    # `exe` command
    exe_parser = subparsers.add_parser("exe", help = "Release: Build an executable with Pyinstaller. You'll need to run `build` first.")
    exe_parser.set_defaults(command = exe)

    # Go
    args = parser.parse_args()
    if len(vars(args)) == 0:
        parser.print_help()
        exit(0)
    
    args.command(modes, toolchains, args)  # pyright: ignore[reportAny]