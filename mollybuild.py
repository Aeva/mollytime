import glob
import os
import platform
import random
import shutil
import subprocess
import string
import sys
import sysconfig
import venv

from argparse import ArgumentParser, Namespace
from configparser import ConfigParser
from pathlib import Path
from subprocess import PIPE
from zipfile import ZipFile

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

def build(modes: dict[str, Path], toolchains: dict[str, Path], args: Namespace):
    # Grab dependencies.
    _get_dependencies([ "meson", "meson-python", "ninja" ])

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

    # HACK: Meson-python isn't smart enough to expose subprojects' DLLs to an editable install.
    # Meson, meanwhile, is too fussy to let us access and manipulate subproject output directly. (For our own good, of course.)
    # Dig them out of the build folder, and put them where the program can find them.
    major, minor, _ = platform.python_version().split(".")
    build_dir = Path("build") / f"cp{major}{minor}"
    subprojects_dir = build_dir / "subprojects"

    for subproject_dir in subprojects_dir.iterdir():
        for file in subproject_dir.iterdir():
            if file.is_file() and file.suffix == ".dll":
                output_path_expected = build_dir / file.name
                output_path_actual = file.copy(output_path_expected)
                print(f"Copied {file} to {output_path_actual}.")

def package(modes: dict[str, Path], toolchains: dict[str, Path], args: Namespace):
    # Grab dependencies.
    _get_dependencies([ "build" ])

    # Prepare to execute `py(thon) -m build`.
    args_dict = vars(args)
    output_dir = Path("dist")
    setup_args = [ sys.executable, "-m", "build", "--outdir", output_dir ]

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
    
    # Build the source distribution & wheel.
    package_result = subprocess.run(setup_args, stdout = PIPE)
    package_result.check_returncode()

    # Dig out the name of the built wheel.
    package_stdout = package_result.stdout.decode().strip()
    package_report = package_stdout.splitlines()[-1]
    if package_report.startswith("Successfully built "):
        wheel_name = package_report.removeprefix("Successfully built ")
    else:
        print("Can't infer wheel name from package report. Inferring...")
        maybe_wheel_names = glob.glob((output_dir / "*.whl").as_posix())
        if len(maybe_wheel_names) < 1:
            raise RuntimeError(f"Couldn't find any built *.whl files in {output_dir}.")
        
        wheel_name = maybe_wheel_names[0]
        print(f"...Inferred {wheel_name}")
    
    wheel_path = output_dir.resolve() / wheel_name

    # Create a temporary directory to work in.
    this_dir = Path(__file__).parent
    temp_id = ''.join(random.choices(string.ascii_lowercase + string.digits, k = 8))
    temp_dir = Path(f".mollybuild-package-{temp_id}")

    try:    
        shutil.rmtree(temp_dir, ignore_errors = True)
        os.makedirs(temp_dir)

        # Create an isolated virtual environment.
        venv_path = temp_dir / ".venv"
        venv.EnvBuilder(with_pip = True).create(venv_path)
        venv_config_vars = sysconfig.get_config_vars().copy()
        venv_config_vars["base"] = venv_path.resolve()
        venv_paths = sysconfig.get_paths(scheme = "venv", vars = venv_config_vars)
        venv_scripts = Path(venv_paths["scripts"])
        venv_python = (venv_scripts / 'python.exe') if os.name == "nt" else "python"

        # Install dependencies into the virtual environment.
        venv_packages = [ wheel_path, "pyinstaller" ]
        venv_pip_command = [ venv_python, "-Im", "pip", "install" ] + venv_packages
        venv_pip_process = subprocess.run(venv_pip_command)
        venv_pip_process.check_returncode()

        # Unzip the wheel.
        wheel_zip = ZipFile(wheel_path)
        wheel_out_path = temp_dir / wheel_name
        _ = wheel_zip.extractall(wheel_out_path)

        # Copy the pyinstaller shim over, and build.
        venv_pyinstaller_main = shutil.copy(this_dir / "pyinstaller_main.py", wheel_out_path / "pyinstaller_main.py")
        venv_pyinstaller_command = [
            venv_scripts / "pyinstaller",
            "--distpath", output_dir,
            "--specpath", temp_dir / ".pyinstaller",
            "--workpath", temp_dir / ".pyinstaller" / "work",
            "--onefile",
            "--name", "mollytime",
            "--icon", this_dir / "mollytime.ico",
            "--copy-metadata", "mollytime",
            "--collect-binaries", "mollytime",
            "--collect-data", "mollytime",
            venv_pyinstaller_main
        ]
        venv_pyinstaller_process = subprocess.run(venv_pyinstaller_command)
        venv_pyinstaller_process.check_returncode()
    
    finally:
        # Clean out the temporary work dir.
        shutil.rmtree(temp_dir, ignore_errors = True)

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

    # Go
    args = parser.parse_args()
    if len(vars(args)) == 0:
        parser.print_help()
        exit(0)
    
    args.command(modes, toolchains, args)  # pyright: ignore[reportAny]