import subprocess

from argparse import ArgumentParser
from pathlib import Path

parser = ArgumentParser(
    prog = "sdl3_build",
    description = "SDL3 build helper."
)
_ = parser.add_argument(
    "--cmake-path",
    help = "Path to a CMake executable. If unspecified, it has to be on PATH.",
    default = "cmake"
)
_ = parser.add_argument(
    "build_dir",
    help = "Where to stash intermediate files."
)

# ---

this_dir = Path(__file__).parent
project_dir = this_dir.parent
sdl3_dir = project_dir / "third_party" / "SDL3-3.4.0"

args = vars(parser.parse_args())
cmake = Path(args["cmake_path"])    # pyright: ignore[reportAny]
build_dir = Path(args["build_dir"]) # pyright: ignore[reportAny]
install_dir = sdl3_dir / "build" / "install"

cmake_configure_process = subprocess.run(
    [
        cmake,
        "-S", sdl3_dir,
        "-B", build_dir,
        "-Wno-dev",
        f"-D CMAKE_INSTALL_PREFIX='{install_dir}'",
        '-D CMAKE_C_COMPILER=clang',
        '-D CMAKE_CXX_COMPILER=clang++',
        '-D CMAKE_LINKER_TYPE=LLD',
        '-D CMAKE_RC_COMPILER=llvm-rc',
        '-D CMAKE_MAKE_PROGRAM=ninja',
        "-G", "Ninja"
    ]
)
cmake_configure_process.check_returncode()

cmake_build_process = subprocess.run([
    cmake,
    "--build", build_dir,
    "--config", "Release"
])
cmake_build_process.check_returncode()

cmake_install_process = subprocess.run([
    cmake,
    "--install", build_dir,
    "--config", "Release"
])
cmake_install_process.check_returncode()
