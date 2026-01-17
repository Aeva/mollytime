import subprocess

from argparse import ArgumentParser
from pathlib import Path

parser = ArgumentParser(
    prog = "build_boost",
    description = "Boost build helper."
)
_ = parser.add_argument(
    "--cmake-path",
    help = "Path to a CMake executable. If unspecified, it has to be on PATH.",
    default = "cmake"
)
_ = parser.add_argument(
    "--cpp-compiler",
    help = "Path to a C++ compiler. If unspecfied, uses the system default."
)
_ = parser.add_argument(
    "--linker-type",
    help = "C/++ linker type. If unspecfied, uses the system default."
)
_ = parser.add_argument(
    "build_dir",
    help = "Where to stash intermediate files."
)

# ---

this_dir = Path(__file__).parent
project_dir = this_dir.parent
boost_dir = project_dir / "third_party" / "boost_1_90_0"

args = vars(parser.parse_args())
cmake = Path(args["cmake_path"])    # pyright: ignore[reportAny]
build_dir = Path(args["build_dir"]) # pyright: ignore[reportAny]
install_dir = boost_dir / "dist"

compiler_args: list[str] = []
if (cpp_compiler := args.get("cpp_compiler", None)):
    compiler_args.append(f"-D CMAKE_CXX_COMPILER={cpp_compiler}")
if (linker_type := args.get("linker_type", None)):
    compiler_args.append(f"-D CMAKE_LINKER_TYPE={linker_type}")

cmake_configure_process = subprocess.run([
    cmake,
    "-S", boost_dir,
    "-B", build_dir,
    "-Wno-dev",
    f"-D CMAKE_INSTALL_PREFIX='{install_dir}'",
    '-D CMAKE_MAKE_PROGRAM=ninja',
    "-G", "Ninja"
] + compiler_args)
cmake_configure_process.check_returncode()

cmake_build_process = subprocess.run([
    cmake,
    "--build", build_dir,
    "--config", "Release"
])
cmake_build_process.check_returncode()

cmake_install_process = subprocess.run([
    cmake,
    "--build", build_dir,
    "--target", "install",
    "--config", "Release"
])
cmake_install_process.check_returncode()